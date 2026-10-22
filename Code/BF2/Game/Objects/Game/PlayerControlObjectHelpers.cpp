// Hierarchy helpers for vehicle cameras, weapons, input targets and entry points.
// [CHOSEN, not recovered] File split; traversal preserves breadth-first, left-to-right order where required.

#include "PlayerControlObject.h"
#include "../ICameraObject.h"

#include <list>

#include "../../../Core/Ids.h"
#include "../ObjectLayoutAccess.h"                // the +0x2C sibling link and +0x14 template
#include "../ObjectTemplate.h"                    // getClassID on the read template

// BFfindEntryPoint
#include "EntryPoint.h"                                   // the -0x138 downcast + this file's decl
#include "../IEntryPointObject.h"                         // the interface the grid hits answer
#include "../IPlayer.h"                                   // getIsAIPlayer (+0x64) / getId (+0x54)
#include "../IObject.h"                                   // getRootParent / checkIfRayHitAnyObject
#include "../TemplateLeaves.h"                            // EntryPointTemplate::m_entryRadius +0x164
#include "../GameLogicComp/IArmorGLComp.h"                 // isDestroyed (+0x54)
#include "../../../Scene/Object/ObjectManager.h"           // objectManager + the two grid queries
#include "../../../Scene/Object/ObjectPredicator.h"        // ObjectFlagPredicator(0x200)
#include "../../../AI/AIInterface/SingletonServer.h"       // BFSingletonServer::m_iaiPlayerCourier
#include "../../../AI/AIInterface/IAIPlayerCourier.h"      // ds:0x8AC35A0 +0x08 - the bot's order
#include <cmath>                                           // the distance
#include <vector>

#if defined(_MSC_VER)
#  define BF2_PCOH_VCALL __thiscall
#else
#  define BF2_PCOH_VCALL
#endif

namespace dice {
namespace hfe {

namespace world {

namespace {

typedef object_detail::LayoutAccess L;

inline IObject* siblingOf(IObject* o) { return const_cast<IObject*>(o->getSibling()); }
inline IObject* childOf(IObject* o)   { return const_cast<IObject*>(o->getChild()); }

// Typed interface queries for hierarchy traversal.
inline IPlayerControlObject* pcoOf(IObject* o)
{ return static_cast<IPlayerControlObject*>(o->queryInterface(world::IID_IPlayerControlObject)); }
inline ICameraObject* cameraOf(IObject* o)
{ return static_cast<ICameraObject*>(o->queryInterface(world::IID_ICameraObject)); }
inline IObject* objectOf(IPlayerControlObject* pco)
{ return static_cast<IObject*>(pco->queryInterface(world::IID_IObject)); }   // ipco +0x0C, as the ELF

} // namespace

// findPlayerControlObjects - 0x81A1C60, 463 B. Every IPlayerControlObject in the subtree
// (root included), keyed by its pcoId. insert_unique: a duplicate id keeps the first found.
void findPlayerControlObjects(IObject* root, std::map<int, IPlayerControlObject*>& out)
{
    if (!root)
        return;
    std::list<IObject*> work;
    work.push_back(root);
    if (IPlayerControlObject* pco = pcoOf(root))
        out.insert(std::make_pair(pco->getPcoId(), pco));

    while (!work.empty())
    {
        IObject* node = work.front();
        work.pop_front();
        for (IObject* child = childOf(node); child; child = siblingOf(child))
        {
            if (IPlayerControlObject* pco = pcoOf(child))
                out.insert(std::make_pair(pco->getPcoId(), pco));
            work.push_back(child);
        }
    }
}

// findSpawnPoints - 0x81200E0, 485 B. Every object in the subtree answering ISpawnPoint - and
// only objects with a template (the +0x14 test) qualify, root included.
void findSpawnPoints(IObject* root, std::vector<IObject*>& out)
{
    if (!root)
        return;
    std::list<IObject*> work;
    work.push_back(root);
    if (L::objectTemplate(root) && root->queryInterface(IID_ISpawnPoint))
        out.push_back(root);

    while (!work.empty())
    {
        IObject* node = work.front();
        work.pop_front();
        for (IObject* child = childOf(node); child; child = siblingOf(child))
        {
            if (L::objectTemplate(child) && child->queryInterface(IID_ISpawnPoint))
                out.push_back(child);
            work.push_back(child);
        }
    }
}

// Preserves authored PCO IDs, assigning each unnumbered position the lowest unused ID in BFS order.
void postInitPcoId(IObject* root)
{
    if (!root)
        return;
    std::list<IObject*> work;
    std::list<IPlayerControlObject*> pending;
    std::map<int, IPlayerControlObject*> byId;
    work.push_back(root);

    if (IPlayerControlObject* pco = pcoOf(root))
    {
        const int id = pco->getPcoId();
        if (id == -1) pending.push_back(pco);
        else          byId.insert(std::make_pair(id, pco));
    }
    while (!work.empty())
    {
        IObject* node = work.front();
        work.pop_front();
        for (IObject* child = childOf(node); child; child = siblingOf(child))
        {
            if (IPlayerControlObject* pco = pcoOf(child))
            {
                const int id = pco->getPcoId();
                if (id == -1) pending.push_back(pco);
                else          byId.insert(std::make_pair(id, pco));
            }
            work.push_back(child);
        }
    }

    // pass 2: the probe. `candidate` climbs monotonically across the whole pending list, as the
    // ELF's single counter does (it is NOT reset per pending entry).
    int candidate = 0;
    while (!pending.empty())
    {
        if (byId.find(candidate) == byId.end())
        {
            IPlayerControlObject* pco = pending.front();
            pending.pop_front();
            pco->setPcoId(candidate);
            byId.insert(std::make_pair(pco->getPcoId(), pco));
        }
        ++candidate;
    }
}

// Collects cameras breadth-first for the requested player-control id.
// Unnamed cameras receive descending negative ids; the first camera for a duplicate id wins.

void findCameras(IObject* root, int pcoId, std::map<int, ICameraObject*>& out)
{
    if (!root)
        return;
    std::list<IObject*> work;
    work.push_back(root);
    int nextUnnumbered = -1;

    if (ICameraObject* cam = cameraOf(root))
    {
        if (cam->getPcoId() == pcoId)
        {
            if (cam->getCameraId())
                out.insert(std::make_pair(cam->getCameraId(), cam));
            else
            {
                cam->setCameraId(-1);
                out.insert(std::make_pair(-1, cam));
                nextUnnumbered = -2;
            }
        }
    }
    while (!work.empty())
    {
        IObject* node = work.front();
        work.pop_front();
        for (IObject* child = childOf(node); child; child = siblingOf(child))
        {
            if (ICameraObject* cam = cameraOf(child))
            {
                if (cam->getPcoId() == pcoId)
                {
                    if (cam->getCameraId())
                        out.insert(std::make_pair(cam->getCameraId(), cam));
                    else
                    {
                        cam->setCameraId(nextUnnumbered);
                        out.insert(std::make_pair(nextUnnumbered, cam));
                        --nextUnnumbered;
                    }
                }
            }
            work.push_back(child);
        }
    }
}

// Collects each seat's cameras and selects the first camera from its effective camera map.
void postInitCameras(IObject* root)
{
    std::map<int, IPlayerControlObject*> pcos;
    findPlayerControlObjects(root, pcos);
    for (std::map<int, IPlayerControlObject*>::iterator it = pcos.begin(); it != pcos.end(); ++it)
    {
        IPlayerControlObject* pcoInterface = it->second;
        IObject* pcoObject = objectOf(pcoInterface);                      // ipco +0x0C qi(IID_IObject)
        std::map<int, ICameraObject*>* ownCameras = pcoInterface->getOwnCameras();   // +0x3C
        findCameras(pcoObject, pcoInterface->getPcoId(), *ownCameras);    // [FRONTIER]

        std::map<int, ICameraObject*>* cameras = pcoInterface->getCameras();   // +0x38 [FRONTIER body] - never null
        if (!cameras->empty())
        {
            ICameraObject* firstCamera = cameras->begin()->second;        // header->left = leftmost
            pcoInterface->setActiveCamera(firstCamera->getCameraId());    // +0x48 <- +0x2C  [FRONTIER body]
        }
    }
}

// Collects child weapons recursively, stopping at nested player-control objects.
void internalCollect(IPlayerControlObject* pco, IObject* child)
{
    if (!child->queryInterface(IID_IObject))
        return;
    if (child->queryInterface(IID_IWeaponObject))
    {
        pco->addWeapon(child);                                                  // ipco +0x68
        return;
    }
    if (child->queryInterface(IID_IPlayerControlObject))
        return;
    for (IObject* c = childOf(child); c; c = siblingOf(c))
        internalCollect(pco, c);
}

void collectWeapons(IPlayerControlObject* pco)
{
    IObject* self = objectOf(pco);                                     // ipco +0x0C qi(IID_IObject)
    for (IObject* child = childOf(self); child; child = siblingOf(child))
        internalCollect(pco, child);
}

// findObjectHavingTemplateId - 0x8197AF0, 84 B. Depth-first, sibling-iterative: this node, then
// its subtree, then its siblings; the first object whose template id matches. Note the parameter
// is the template's ID (an unsigned), compared against getId() (template vcall +0x44).
IObject* findObjectHavingTemplateId(IObject* node, unsigned int templateId)
{
    while (node)
    {
        const IObjectTemplate* t = L::objectTemplate(node);
        if (t && t->getId() == templateId)
            return node;
        if (IObject* found = findObjectHavingTemplateId(childOf(node), templateId))
            return found;
        node = siblingOf(node);
    }
    return 0;
}


// Finds camera, wing, engine, force, floating and rotational input targets breadth-first.
// Nested player-control objects bound traversal; the root-match flag may stop at the root.
namespace {
inline bool isInputDrivenTemplate(IObject* o)
{
    const IObjectTemplate* t = L::objectTemplate(o);
    if (!t) return false;
    const unsigned int cid = static_cast<const ObjectTemplate*>(t)->getClassID();
    return cid == CID_CameraTemplate || cid == CID_WingTemplate || cid == CID_EngineTemplate ||
           cid == CID_ForceObjectTemplate || cid == CID_FloatingBundleTemplate ||
           cid == CID_RotationalBundleTemplate;
}
} // namespace

void findPlayerInputObjects(IObject* root, std::list<IObject*>& out, bool stopAtMatchingRoot)
{
    if (!root)
        return;
    std::list<IObject*> work;
    if (isInputDrivenTemplate(root))
    {
        out.push_back(root);
        if (stopAtMatchingRoot)
            return;
    }
    work.push_back(root);

    while (!work.empty())
    {
        IObject* node = work.front();
        work.pop_front();
        for (IObject* child = childOf(node); child; child = siblingOf(child))
        {
            if (isInputDrivenTemplate(child))
            {
                out.push_back(child);
                work.push_back(child);
            }
            else if (!child->queryInterface(world::IID_IPlayerControlObject))
            {
                work.push_back(child);               // plain part: descend, do not collect
            }
            // nested PCO: boundary
        }
    }
}


// Returns the closest usable entry point matching the requested template, or null.
// Filters by distance, armor, team and line of sight, preferring empty seats over bot occupants.
// AI entry uses its own radius and vehicle rules.
IPlayerControlObject* BFfindEntryPoint(const Vec3& position, float radius, int team,
                                       IPlayer* player, bool useEntryPointRadius,
                                       bool acceptAiOccupied, bool requireLineOfSight)
{
    bool     isAI               = false;
    IObject* aiRequestedVehicle = 0;

    if (player && player->getIsAIPlayer())                          // IPlayer +0x64
    {
        radius              = 15.0f;                                // flt_888D944 overrides the caller
        isAI                = true;
        useEntryPointRadius = false;
        // Retail has no null check on the courier. [CHOSEN, not recovered] openbf2-server-ext
        // docs/protocol.md R1b: without the AI library the courier is null, which is treated as
        // no requested vehicle, so the AI-flagged player takes the nearest free seat in range.
        if (ai::BFSingletonServer::m_iaiPlayerCourier)
            aiRequestedVehicle = static_cast<ai::IAIPlayerCourier*>(
                ai::BFSingletonServer::m_iaiPlayerCourier)
                    ->getRequestedVehicle(player->getId());         // ds:0x8AC35A0 +0x08 / IPlayer +0x54
    }

    // the two grid passes, copied out of the manager's shared scratch vector
    std::vector<IObject*>& scratch = objectManager->getObjectTempVector();          // +0xA8
    scratch.erase(scratch.begin(), scratch.end());                                  // retail's clear()

    IPlayerControlObject* best     = 0;
    float                 bestDist = 100000.0f;                                     // flt_888F0AC
    IPlayerControlObject* alternate     = 0;
    float                 alternateDist = 100000.0f;

    ObjectFlagPredicator predicator(0x200);
    std::vector<IObject*> candidates;

    objectManager->getObjectsFromGrid(position, radius, scratch, &predicator);            // +0x40
    for (unsigned int i = 0; i < scratch.size(); ++i)
        candidates.push_back(scratch[i]);

    objectManager->getNoCollisionObjectsFromGrid(position, radius, scratch, &predicator); // +0x44
    for (unsigned int i = 0; i < scratch.size(); ++i)
        candidates.push_back(scratch[i]);

    for (unsigned int c = 0; c < candidates.size(); ++c)
    {
        IObject* const object = candidates[c];

        IEntryPointObject* const entryPointObject = static_cast<IEntryPointObject*>(
            object->queryInterface(IID_IEntryPointObject));                          // +0x0C
        if (!entryPointObject)
            continue;
        if (L::objectTemplate(object)->getClassID() != CID_EntryPointTemplate)        // raw +0x14
            continue;

        // the -0x138 downcast the ELF compiles: IEntryPointObject is EntryPoint's second base.
        IPlayerControlObject* const vehicle =
            static_cast<EntryPoint*>(entryPointObject)->getLinkedPlayerControlObject();  // +0x18C
        if (!vehicle)
            continue;

        // gate 3/4: how far away is it
        Vec3 from;
        from.x = position.x; from.y = position.y; from.z = position.z;
        const Vec3& absolute = object->getAbsolutePosition();                        // +0x6C
        Vec3 to;
        to.x = absolute.x; to.y = absolute.y; to.z = absolute.z;
        Vec3 delta;
        delta.x = to.x - from.x;
        delta.y = to.y - from.y;
        delta.z = to.z - from.z;
        const float distance =
            std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

        if (useEntryPointRadius)
        {
            // the field is read RAW in both ports (0x81A35AE: `fld dword ptr [eax+164h]`);
            // EntryPointTemplate has no getter symbol of its own.
            if (distance > static_cast<const EntryPointTemplate*>(
                               L::objectTemplate(object))->m_entryRadius)             // +0x164
                continue;
        }
        else if (distance > radius)
        {
            continue;
        }
        if (distance > bestDist)
            continue;

        // gate 5: the nearest armour up the parent chain must not be destroyed
        IArmorGLComp* armor = 0;
        for (IObject* node = object; node; node = L::parent(node))                   // +0x28
        {
            armor = static_cast<IArmorGLComp*>(L::armorComponent(node));             // +0x3C
            if (armor)
                break;
        }
        if (armor && armor->isDestroyed())                                           // +0x54
            continue;

        // gate 6: whose vehicle is it
        // the `vehicle ?` test is redundant - it was proved non-null above - but retail compiles
        // it (0x81A33CC), so it is kept.
        IObject* const root = getRootParent(
            vehicle ? static_cast<IObject*>(vehicle->queryInterface(IID_IObject)) : 0);

        IPlayerControlObject* owner = 0;
        for (IObject* node = root; node; node = L::parent(node))                     // +0x28
        {
            owner = static_cast<IPlayerControlObject*>(
                node->queryInterface(IID_IPlayerControlObject));                     // +0x0C
            if (owner)
                break;
        }
        // NO NULL guard ON `owner`, as shipped - see the header block.
        if (team != owner->getTeam() && owner->getTeam() != 0)                       // +0x98 twice
            continue;

        // gate 7: can the player actually see it
        if (!isAI && requireLineOfSight)
        {
            const Vec3& target = object->getAbsolutePosition();                      // +0x6C again
            Vec3 rayTo;
            rayTo.x = target.x; rayTo.y = target.y; rayTo.z = target.z;
            Vec3 rayFrom;
            rayFrom.x = position.x; rayFrom.y = position.y; rayFrom.z = position.z;
            if (checkIfRayHitAnyObject(rayFrom, rayTo, root, true))    // ; object-line LOS live
                continue;
        }

        // the seat sweep
        std::map<int, IPlayerControlObject*>* const seats = vehicle->getPcos();       // +0x60
        for (std::map<int, IPlayerControlObject*>::iterator seat = seats->begin();
             seat != seats->end(); ++seat)
        {
            IPlayerControlObject* const candidate = seat->second;

            if (aiRequestedVehicle &&
                aiRequestedVehicle != candidate->queryInterface(IID_IObject))         // +0x0C
                continue;

            if (!candidate->isOccupied())                                             // +0x18
            {
                best     = candidate;
                bestDist = distance;
                break;                        // the seat loop only; candidates keep coming
            }

            if (!isAI && acceptAiOccupied)
            {
                // getOccupingPlayer() is dereferenced unguarded (dice's spelling), guarded only
                // by the isOccupied() above.
                if (candidate->getOccupingPlayer()->getIsAIPlayer() &&                // +0x10 +0x64
                    alternateDist > distance)
                {
                    alternate     = candidate;
                    alternateDist = distance;
                }
            }
        }
    }

    return best ? best : alternate;
}

} // namespace world
} // namespace hfe
} // namespace dice
