// Default projectile target-following behavior.

#include "DefaultFollowComp.h"

#include <cfloat>
#include <cmath>
#include <new>
#include <sstream>
#include <string>

#include "../ProjectileCompTemplates.h"
#include "../../Base/BundleTemplate.h"
#include "../../IObject.h"
#include "../../IObjectTemplate.h"
#include "../../ObjectLayoutAccess.h"
#include "../../../Utility/Vars.h"
#include "../../../../Core/Ids.h"
#include "../../../../Core/System.h"
#include "../../../../IO/Stream/IStream.h"
#include "../../../../Physics/IPhysicsNode.h"
#include "../../CompTemplateBases.h"
#include "../../IWeaponObject.h"

namespace dice {
namespace hfe {
namespace world {

namespace {

float* const g_predictionFactor =
    Vars::getFloat("follow-component-prediction-factor", 0.1f);
// This cvar is registered by the shipped TU but has no reader in either followTarget body.
float* const g_targetYOffset = Vars::getFloat("targetYOffset", 0.0f);
float* const g_maxDistKillMissile = Vars::getFloat("maxDistKillMissile", 15.0f);

// [CHOSEN, not recovered] Local serverext/r1 change, not for upstream (no issue). 0 keeps retail.
// Non-zero: a TV missile whose launcher watches its camera keeps turning towards an aim point
// beside or behind it instead of flying straight. See followTarget.
int* const g_tvTurnBehind = Vars::getInt("tv-turn-behind", 0);

// Returns whether the projectile was fired by a TV-guided launcher.
bool isTvGuided(IObject* projectile)
{
    IObject* const launcher = static_cast<GenericProjectile*>(projectile)->getLauncherWeapon();
    IWeaponObject* const weapon = launcher
        ? static_cast<IWeaponObject*>(launcher->queryInterface(IID_IWeaponObject)) : 0;
    return weapon && weapon->getTargetSystemType() == TSTVGuided;
}

float length(const Vec3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    const Vec3 result = {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
    return result;
}

// Exact Vec3::normalize semantics used at PE 0x45BFA0 and inlined by the ELF. The return value
// (-20001 on a degenerate vector) is ignored by every call in this component.
void normalize(Vec3& value)
{
    const float lengthSquared =
        value.x * value.x + value.y * value.y + value.z * value.z;
    if (std::fabs(lengthSquared - 1.0f) >= FLT_EPSILON)
    {
        if (std::fabs(lengthSquared) < FLT_EPSILON)
        {
            value.x = 0.0f;
            value.y = 0.0f;
            value.z = 0.0f;
        }
        else
        {
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            value.x *= inverseLength;
            value.y *= inverseLength;
            value.z *= inverseLength;
        }
    }
}

float clampUnit(float value)
{
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

// PE outlines this operation twice at 0x5BB490/0x5BB500; the ELF inlines it. Parent state is
// sampled independently for pitch and yaw, and even an epsilon-small angle writes the unchanged
// transformation back through the corresponding setter.
void rotateObject(IObject* object, float degrees, unsigned int axisOffset)
{
    const bool hasParent = object_detail::LayoutAccess::parent(object) != 0;
    Mat4 transformation = hasParent
        ? object->getRelativeTransformation()
        : object->getAbsoluteTransformation();

    if (std::fabs(degrees) > FLT_EPSILON)
    {
        const Vec3 axis = {
            transformation.m[axisOffset],
            transformation.m[axisOffset + 1],
            transformation.m[axisOffset + 2]
        };
        Mat4 adjustment = Mat4::getIdentity();
        setRotateAboutLine(adjustment, degrees, axis);
        const Mat4 original = transformation;
        transformation.rotMult(original, adjustment);
    }

    if (hasParent)
        object->setRelativeTransformation(transformation);
    else
        object->setAbsoluteTransformation(transformation);
}

// Rebuild only the first three components of the basis rows. The fourth components and the
// translation row remain exactly as the current absolute transformation supplied them.
void makeHorizontalBasis(Mat4& transformation)
{
    Vec3 forward = {
        transformation.m[8], transformation.m[9], transformation.m[10]
    };
    const Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = cross(up, forward);
    normalize(right);
    Vec3 correctedUp = cross(forward, right);
    normalize(correctedUp);

    transformation.m[0] = right.x;
    transformation.m[1] = right.y;
    transformation.m[2] = right.z;
    transformation.m[4] = correctedUp.x;
    transformation.m[5] = correctedUp.y;
    transformation.m[6] = correctedUp.z;
}

void makeDownwardBasis(Mat4& transformation)
{
    const Vec3 forward = { 0.0f, -1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 up = cross(forward, right);
    normalize(up);
    right = cross(up, forward);
    normalize(right);

    transformation.m[0] = right.x;
    transformation.m[1] = right.y;
    transformation.m[2] = right.z;
    transformation.m[4] = up.x;
    transformation.m[5] = up.y;
    transformation.m[6] = up.z;
    transformation.m[8] = forward.x;
    transformation.m[9] = forward.y;
    transformation.m[10] = forward.z;
}

} // namespace

DefaultFollowComp::DefaultFollowComp(DefaultFollowCompTemplate* objectTemplate)
    : m_refCount(1)
    , m_template(objectTemplate)
    , m_yawTurn(0.0f)
    , m_pitchTurn(0.0f)
    , m_targetObject()
    , m_delayTick(0)
    , m_initialSpeedMagnitude(0.0f)
    , m_firstFollow(true)
    , m_previousSpeed()
    , m_terminal(false)
    , m_killDistanceReached(false)
{
    ProjectileComp::m_refCount = 1;
    m_object = 0;
    m_initObject = 0;
}

DefaultFollowComp::~DefaultFollowComp() {}

void DefaultFollowComp::addRef() { ++m_refCount; }
int DefaultFollowComp::getRef() { return m_refCount; }

int DefaultFollowComp::release()
{
    const int remaining = m_refCount - 1;
    const bool last = (m_refCount == 1);
    m_refCount = remaining;
    if (last && this)
        delete this;
    return remaining;
}

void* DefaultFollowComp::queryInterface(unsigned int iid) const
{
    DefaultFollowComp* const self = const_cast<DefaultFollowComp*>(this);
    if (iid == IID_IBase || iid == IID_IObjectComponent)
        return static_cast<IObjectComponent*>(self);
    if (iid == IID_IProjectileComp || iid == IID_IProjectileFollowComp)
        return static_cast<IProjectileFollowComp*>(self);
    return 0;
}

unsigned int DefaultFollowComp::getInterfaceID() const
{
    return IID_IProjectileFollowComp;
}

// Yes, false: GenericProjectile drives its follow component directly. Both shipped ports retain a
// real updateComponent body despite this answer.
bool DefaultFollowComp::hasUpdate() const
{
    return false;
}

void DefaultFollowComp::updateComponent(float, unsigned int)
{
    IObject* const target = m_targetObject.get();
    if (target && m_delayTick <= static_cast<unsigned int>(WorldPref::getTickCount()))
        (void)followTarget(target);
}

// Retail resets only these fields. The turn values, delay tick, first speed sample, and
// kill-distance latch are deliberately not reset here.
void DefaultFollowComp::activate()
{
    m_targetObject = WeakPtr<IObject>();
    m_terminal = false;
    m_previousSpeed.x = 0.0f;
    m_previousSpeed.y = 0.0f;
    m_previousSpeed.z = 0.0f;
    m_firstFollow = true;
    m_initialSpeedMagnitude = 0.0f;
}

void DefaultFollowComp::setTargetObject(IObject* target)
{
    m_targetObject = target ? target->getWeakPtr() : WeakPtr<IObject>();
}

void DefaultFollowComp::setDelayTick(unsigned int tick)
{
    m_delayTick = tick;
}

bool DefaultFollowComp::followTarget(IObject* target)
{
    if (!target)
        return false;
    if (m_terminal)
        return true;

    // Sample before testing the new distance. Crossing the threshold this call raises the latch,
    // but the straight-through terminal arm is entered only on the following call.
    const bool wasKillDistanceReached = m_killDistanceReached;

    // The flag belongs to the target's immediate parent template, not the target template and not
    // a root-parent replacement. Both ports perform this exact two-step raw walk.
    IObject* const targetParent = object_detail::LayoutAccess::parent(target);
    if (targetParent)
    {
        const IObjectTemplate* const parentTemplate =
            object_detail::LayoutAccess::objectTemplate(targetParent);
        BundleTemplate* const bundleTemplate = parentTemplate
            ? static_cast<BundleTemplate*>(
                  const_cast<IObjectTemplate*>(parentTemplate)
                      ->queryImplementation(CID_BundleTemplate))
            : 0;
        if (bundleTemplate && bundleTemplate->getKillHeatSeekingMissile())
        {
            const Vec3& projectilePosition = m_initObject->getAbsolutePosition();
            const Vec3& targetPosition = target->getAbsolutePosition();
            const Vec3 separation = {
                targetPosition.x - projectilePosition.x,
                targetPosition.y - projectilePosition.y,
                targetPosition.z - projectilePosition.z
            };
            if (length(separation) < *g_maxDistKillMissile)
                m_killDistanceReached = true;
        }
    }

    if (wasKillDistanceReached)
    {
        IPhysicsNode* const node = static_cast<IPhysicsNode*>(
            object_detail::LayoutAccess::physicsNode(m_initObject));
        if (node)
        {
            const float speed = length(node->getPositionalSpeed());
            const Mat4& transformation = m_initObject->getAbsoluteTransformation();
            const Vec3 redirected = {
                transformation.m[8] * speed,
                transformation.m[9] * speed,
                transformation.m[10] * speed
            };
            node->setPositionalSpeed(redirected);
        }
        return true;
    }

    const Vec3& projectilePositionRef = m_initObject->getAbsolutePosition();
    const Vec3 projectilePosition = projectilePositionRef;
    const Vec3& firstTargetPosition = target->getAbsolutePosition();
    Vec3 direction = {
        firstTargetPosition.x - projectilePosition.x,
        firstTargetPosition.y - projectilePosition.y,
        firstTargetPosition.z - projectilePosition.z
    };

    IObject* const targetRoot = getRootParent(target);
    IPhysicsNode* const targetNode = static_cast<IPhysicsNode*>(
        object_detail::LayoutAccess::physicsNode(targetRoot));
    if (targetNode)
    {
        const Vec3& targetVelocity = targetNode->getAbsolutePositionalSpeed();
        const Math::Vec3* const projectileVelocity =
            static_cast<GenericProjectile*>(m_initObject)->getVelocity();
        const float arrivalTime = length(direction) / length(*projectileVelocity);

        // Retail calls getAbsolutePosition on the target again here rather than reusing the first
        // result; keep that observable virtual call.
        const Vec3& targetPosition = target->getAbsolutePosition();
        const float predictionScale = arrivalTime * *g_predictionFactor;
        const Vec3 predictedPosition = {
            targetPosition.x + targetVelocity.x * predictionScale,
            targetPosition.y + targetVelocity.y * predictionScale,
            targetPosition.z + targetVelocity.z * predictionScale
        };
        direction.x = predictedPosition.x - projectilePosition.x;
        direction.y = predictedPosition.y - projectilePosition.y;
        direction.z = predictedPosition.z - projectilePosition.z;
    }

    const float distance = length(direction);
    const Mat4& transformation = m_initObject->getAbsoluteTransformation();

    // The retail code constructs a full transpose then multiplies by its 3x3 rotation. Writing the
    // three equivalent row dots directly avoids inventing a missing Mat4::setTranspose declaration.
    Vec3 localDirection = {
        transformation.m[0] * direction.x
            + transformation.m[1] * direction.y
            + transformation.m[2] * direction.z,
        transformation.m[4] * direction.x
            + transformation.m[5] * direction.y
            + transformation.m[6] * direction.z,
        transformation.m[8] * direction.x
            + transformation.m[9] * direction.y
            + transformation.m[10] * direction.z
    };
    if (distance != 0.0f)
    {
        const float inverseDistance = 1.0f / distance;
        localDirection.x *= inverseDistance;
        localDirection.y *= inverseDistance;
        localDirection.z *= inverseDistance;
    }

    float yawAngle =
        std::asin(clampUnit(localDirection.x)) * 57.2957763671875f;
    const float pitchAngle =
        std::fabs(std::asin(clampUnit(localDirection.y)) * 57.2957763671875f);

    // [CHOSEN, not recovered] tv-turn-behind. Retail stops turning when the aim point is not in
    // front (local z <= 0), so a TV gunner who looks more than 90 degrees away flies straight.
    // This keeps turning towards it at the same caps, only for a TV missile whose launcher watches
    // its camera, so a missile nobody steers keeps flying straight past its last aim point. asin
    // stops at 90 degrees: the aim point's full horizontal angle is taken instead (right if
    // straight behind); its elevation stays as retail measures it.
    const bool turnBehind = *g_tvTurnBehind != 0 && localDirection.z <= 0.0f
        && isTvGuided(m_initObject)
        && static_cast<GenericProjectile*>(m_initObject)->isInProjectileCamera();
    if (turnBehind)
        yawAngle = std::atan2(localDirection.x, localDirection.z) * 57.2957763671875f;

    if (localDirection.z <= 0.0f && !turnBehind)
    {
        m_yawTurn = 0.0f;
        m_pitchTurn = 0.0f;
    }
    else
    {
        float yaw = m_template->getMaxYaw();
        const float absoluteYaw = std::fabs(yawAngle);
        if (yaw > absoluteYaw)
            yaw = absoluteYaw;
        m_yawTurn = localDirection.x < 0.0f ? -yaw : yaw;

        float pitch = m_template->getMaxPitch();
        if (pitch > pitchAngle)
            pitch = pitchAngle;
        m_pitchTurn = localDirection.y > 0.0f ? -pitch : pitch;
    }

    if (m_pitchTurn == 0.0f && m_yawTurn == 0.0f)
        return false;

    // Pitch first about basis row 0, then yaw about the freshly re-read basis row 1.
    rotateObject(m_initObject, m_pitchTurn, 0);
    rotateObject(m_initObject, m_yawTurn, 4);

    Mat4 alignedTransformation = m_initObject->getAbsoluteTransformation();
    IPhysicsNode* const projectileNode = static_cast<IPhysicsNode*>(
        object_detail::LayoutAccess::physicsNode(m_initObject));

    if (projectileNode)
    {
        // Two virtual calls are intentional: retail retains the first reference for the later dot
        // or copy, then calls again to calculate the magnitude.
        const Vec3& currentSpeed = projectileNode->getPositionalSpeed();
        const float currentMagnitude = length(projectileNode->getPositionalSpeed());
        if (m_firstFollow)
        {
            m_initialSpeedMagnitude = currentMagnitude;
            m_firstFollow = false;
            m_previousSpeed = currentSpeed;
        }
        else if (dot(currentSpeed, m_previousSpeed) < 0.0f)
        {
            m_terminal = true;
            const Vec3 fallingSpeed = { 0.0f, -1.0f, 0.0f };
            projectileNode->setPositionalSpeed(fallingSpeed);
            m_initObject->updateFlags(0, 0x1000u);

            makeDownwardBasis(alignedTransformation);
            m_initObject->setAbsoluteTransformation(alignedTransformation);
            m_initObject->handleMessage(static_cast<TemplateMessage>(5), 0, false);
            return true;
        }
        else
        {
            // This arm also handles unordered/NaN dot products, matching the x87 branch.
            m_previousSpeed = currentSpeed;
        }
    }

    makeHorizontalBasis(alignedTransformation);
    m_initObject->setAbsoluteTransformation(alignedTransformation);

    if (projectileNode)
    {
        const float speed = length(projectileNode->getPositionalSpeed());
        const Mat4& finalTransformation = m_initObject->getAbsoluteTransformation();
        const Vec3 redirected = {
            finalTransformation.m[8] * speed,
            finalTransformation.m[9] * speed,
            finalTransformation.m[10] * speed
        };
        projectileNode->setPositionalSpeed(redirected);
    }
    return true;
}

// DefaultFollowCompTemplate.
DefaultFollowCompTemplate::DefaultFollowCompTemplate()
    : m_refCount(1)
    , m_maxYaw(1.0f)
    , m_maxPitch(1.0f)
    , m_changeYaw(0.1f)
    , m_changePitch(0.1f)
    , m_minDist(50.0f)
{
}

void DefaultFollowCompTemplate::addRef() { ++m_refCount; }
int DefaultFollowCompTemplate::getRef() { return m_refCount; }

int DefaultFollowCompTemplate::release()
{
    const int remaining = --m_refCount;
    if (remaining == 0)
        ::operator delete(this);
    return remaining;
}

void* DefaultFollowCompTemplate::queryInterface(unsigned int iid) const
{
    DefaultFollowCompTemplate* const self = const_cast<DefaultFollowCompTemplate*>(this);
    if (iid == IID_IBase
        || iid == IID_IObjectTemplateComponent
        || iid == IID_IProjectileCompTemplate)
    {
        return static_cast<IObjectTemplateComponent*>(self);
    }
    if (iid == io::IID_IConsoleSaveable)
        return static_cast<io::IConsoleSaveable*>(self);
    return 0;
}

unsigned int DefaultFollowCompTemplate::getClassID() const
{
    return CID_DefaultFollowCompTemplate;
}

unsigned int DefaultFollowCompTemplate::getInterfaceID()
{
    return IID_IProjectileFollowCompTemplate;
}

IObjectComponent* DefaultFollowCompTemplate::createComponent()
{
    return new DefaultFollowComp(this);
}

void DefaultFollowCompTemplate::objectCreated(IObject*) {}
void DefaultFollowCompTemplate::objectDestroyed(IObject*) {}
void DefaultFollowCompTemplate::setTemplate(IObjectTemplate*) {}

void* DefaultFollowCompTemplate::queryImplementation(unsigned int classId)
{
    return classId == getClassID() ? this : 0;
}

bool DefaultFollowCompTemplate::makeScript(io::IStream* stream)
{
    std::ostringstream script;
    script << "ObjectTemplate.follow.maxYaw " << m_maxYaw << std::endl;
    script << "ObjectTemplate.follow.maxPitch " << m_maxPitch << std::endl;
    script << "ObjectTemplate.follow.changePitch " << m_changePitch << std::endl;
    script << "ObjectTemplate.follow.changeYaw " << m_changeYaw << std::endl;
    script << "ObjectTemplate.follow.minDist " << m_minDist << std::endl;

    const std::string text = script.str();
    return stream->write(text.c_str(), static_cast<unsigned int>(text.length())) == text.length();
}

} // namespace world
} // namespace hfe
} // namespace dice
