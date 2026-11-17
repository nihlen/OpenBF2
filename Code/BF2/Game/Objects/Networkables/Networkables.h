// Object replication components sharing the NetworkableBase and NetworkableObjectBase interfaces.
// Per-class declarations preserve their individual virtual overrides. [INFERRED] Combined header name.

#ifndef IO_NETWORK_NETWORKABLES_H     // [INFERRED] guard style
#define IO_NETWORK_NETWORKABLES_H

#include "../../../IO/Network/NetworkableBase.h"

#include "../../../Math/Vec3.h"      // the BaseLine structs
#include "../../../Math/Quat.h"      // GenericProjectile's baseline rotation
#include "../../../IO/Network/INetworkableObject.h"   // the second base,
#include "../../../IO/Network/NetworkStats.h"         // KitNetworkable::m_stats,

namespace dice {
namespace hfe {

namespace world { class IObject; class OverheadCamera; class PhysicsSpring; class TargetObject;
                  class Soldier;
                  class GenericProjectile;
                  class DestroyableWindowsBundle; }


// The shared block: destructor plus the nine methods every one of the sixteen overrides.
// getMinimumBitSize's return is [INFERRED] — the body is 10 bytes (`mov eax, imm; retn`), so it
// returns an integer constant, but nothing recovered pins the width or signedness.
#define BF2_NETWORKABLE_CONTRACT(cls)                                                       \
    virtual ~cls();                                                                         \
    virtual void  addRef();                                                                 \
    virtual int   getRef();                                                                 \
    virtual int   release();                                                                \
    virtual void* queryInterface(unsigned int) const;                                       \
    virtual bool  init(void*);                                                              \
    virtual void  updateStateMask(unsigned int* stateMask);                                 \
    virtual bool  getNetUpdate(io::BitStream&, io::BaseLineData*, unsigned int, int);       \
    virtual void  setNetUpdate(io::BitStream&, float updateTime, bool ghostActive,          \
                               io::BaseLineData*, int);                                     \
    virtual unsigned int getMinimumBitSize() const

// The sixteen. `predict` is declared only where it has a real body — the nine classes that inherit
// the five-byte stub do not appear to override it, and declaring it for them would misrepresent the
// vtable. Sizes in the comments are the ELF body sizes and are the quickest guide to how much work
// each one is.

// predict and setOwnerID belong to the secondary INetworkableObject interface.

// Destroyable-object replication; inherits reset and baseline behavior.
class DestroyableObjectNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(DestroyableObjectNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          DestroyableObjectNetworkable();
          virtual float getAverageUpdateDeltaTime() const;
          virtual void  predict(float);                             // — empty stub

    // Baseline records have 24-byte stride; the object belongs to NetworkableObjectBase.
    struct BaseLine
    {
        /* +0x00 */ float time;                         // the shared header - never written here
        /* +0x04 */ bool  byte04;                       // "        "      - nothing reads it
        /* +0x08 */ float hitPoints;                    // ArmorGLComp::getHitPoints()
        /* +0x0C */ Vec3  lastHitDirection;             // ArmorGLComp::getLastHitDirection()
    };
    /* +0x50 */ int       m_refCount;
    /* +0x54 */ BaseLine* m_baselines;                  // [4]

    static io::RegistredNetworkStats m_stats;           // "DestroyableObjectNetworkable",
};                                                      // colour (10, 140, 10)

// Destroyable-window replication.
class DestroyableWindowsBundleNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(DestroyableWindowsBundleNetworkable);
          DestroyableWindowsBundleNetworkable();
          static IBase* create(unsigned int iid, IBase* outer);
          virtual void predict(float);                              // — empty stub

    // members with updateStateMask. 12-byte stride (`12 * slot`), and the extra
    // four bytes over the 88 B classes are the object pointer at +0x58.
    struct BaseLine
    {
        /* +0x00 */ float time;                         // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ unsigned int paneState;             // geometry-part mask
    };
    /* +0x50 */ int             m_refCount;
    /* +0x54 */ BaseLine*       m_baselines;            // [4]
    /* +0x58 */ world::DestroyableWindowsBundle* m_bundle;
    static io::RegistredNetworkStats m_stats;
};

// Engine-state replication.
class EngineNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(EngineNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          EngineNetworkable();

    // members with updateStateMask. 60-byte stride (`60 * slot`), and the extra
    // eight bytes over the 88 B classes are the engine pointer at +0x58 and the ring at +0x5C.
    struct Record
    {
        /* +0x00 */ float         time;             // the shared header
        /* +0x04 */ bool          byte04;
        /* +0x05 */ unsigned char pad05;            // [UNRESOLVED] - never touched
        /* +0x06 */ unsigned char byte06;           // [UNRESOLVED] writer - predict() copies it to
                                                    // engine +0x1B5; nothing recovered stores it
        /* +0x07 */ unsigned char pad07;            // [UNRESOLVED] - never touched
        /* +0x08 */ int           currentGear;      // engine +0x1E8 - bit 1
        /* +0x0C */ int           dword0C;          // [UNRESOLVED] - untouched here
        /* +0x10 */ float         throttle;         // clamp(engine +0x1C8, -1, 1) - bit 7
        /* +0x14 */ float         angularRate;      // angularRate()->z - bit 4
        /* +0x18 */ float         forwardSpeed;     // linearSpeed()->z - bit 2
        /* +0x1C */ float         lateralSpeed;     // linearSpeed()->x - bit 3
        /* +0x20 */ float         field1C4;         // [UNRESOLVED] - bit 5
        /* +0x24 */ float         field1D4;         // [UNRESOLVED] - bit 6
        /* +0x28 */ float         field1D8;         // [UNRESOLVED] - bit 8, gated
        /* +0x2C */ unsigned char aboveThreshold;   // |engine +0x160| > 0.01 - bit 9
        /* +0x2D */ unsigned char isNegative;       // engine +0x160 < 0 - rides on bit 9
        /* +0x30 */ float         excessRpm;        // bit 10 - and the bit it clears
        /* +0x34 */ unsigned char differentialNegative;
        // Fraction quantized to eight bits on the wire.
        /* +0x38 */ float         field1E4;         // [UNRESOLVED] meaning - never compared
    };
    /* +0x50 */ int             m_refCount;
    /* +0x54 */ int             m_dword54;          // [UNRESOLVED] - create() leaves it unwritten;
                                                    // nulled in the ctor here [CHOSEN]
    /* +0x58 */ world::IObject* m_engine;           // the engine object - init() stores it
    /* +0x5C */ Record*         m_records;          // [4]
          virtual void reset();
          virtual unsigned int getGhostStateMask() const;           // 0x327
          virtual bool getBaseLine(io::BaseLineData&);
          virtual void predict(float);                              // real body
          void dumpState(int);                                      // empty

    static io::RegistredNetworkStats m_stats;       // "EngineNetworkable", (220,220,220)
};

class FreeCameraNetworkable : public io::NetworkableBase
{ public: BF2_NETWORKABLE_CONTRACT(FreeCameraNetworkable);
          virtual bool getBaseLine(io::BaseLineData&);
          virtual void predict(float);
};

// Replicates projectile state and manages client interpolation from a locally predicted stand-in.
class GenericProjectileNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(GenericProjectileNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          GenericProjectileNetworkable();                           // [INFERRED] from create's stores

    // 80-byte projectile snapshot; velocity scratch is derived from prior-record deltas.
    struct Record
    {
        /* +0x00 */ float         time;             // the shared header
        /* +0x04 */ bool          byte04;
        /* +0x08 */ Vec3          position;         // bit 1
        /* +0x14 */ Quat          rotation;         // bit 0 - compared with memcmp
        /* +0x24 */ unsigned char detonate;         // bit 2
        /* +0x28 */ int           field1C4;         // projectile +0x1C4 - bit 2 as well
        /* +0x2C */ Vec3          vector1CC;        // projectile +0x1CC - bit 2 as well
        /* +0x38 */ Vec3          predictVelocity;  // predict's scratch - never on the wire
        /* +0x44 */ int           hitPointsCeil;    // ceil(ArmorGLComp::getHitPoints()) - bit 4
        /* +0x48 */ unsigned char hasTargetNetworkable;
        /* +0x4C */ int           targetNetworkableId;   // INetworkable::m_networkableId (16-bit)
    };
    /* +0x50 */ int     m_refCount;
    /* +0x54 */ Record* m_records;                  // [4] - operator new[](0x140), init()
    // Prediction timestamp reset to -1; predict latches the first pass time.
    /* +0x58 */ float   m_predictTime;              // [INFERRED name]
    // [CHOSEN, not recovered] tv-camera-lead and tv-path-lead: the projectile's true rotation
    // (as a quaternion and as the transform's 3x3 rows) and position at the last two ticks it was
    // snapshotted in (GenericProjectileNetworkable.cpp).
    Quat         m_leadPrev;
    Quat         m_leadCur;
    float        m_leadPrevRows[9];
    float        m_leadCurRows[9];
    Vec3         m_leadPrevPos;
    Vec3         m_leadCurPos;
    unsigned int m_leadPrevTick;
    unsigned int m_leadCurTick;
    bool         m_leadPrimed;
    float        m_leadAutoMs;                  // tv-path-lead -1: the smoothed gunner view lag, -1 unset
          virtual void reset();                                     // re-primes slot 0
          virtual void setGhostActive(bool);                        // unique to this class
          virtual float getAverageUpdateDeltaTime() const;
          virtual void predict(float);                              // client
          // . Returns the proxy (was declared void): both images
          // hand the created object back in eax and predict takes its weak pointer. The pointee
          // type is [INFERRED] from the body - see the definition.
          world::GenericProjectile* createInterpolationProxy(world::GenericProjectile*);

          // the house ring search, over the 80-byte stride.
          bool findEmbracingIndices(float t, unsigned int* lo, unsigned int* hi) const;

    static io::RegistredNetworkStats m_stats;       // "GenericProjectileNetworkable",
};                                                  // colour (0, 250, 0)

// second base io::NetworkableObjectBase sits at +0x3C in all sixteen.
class ItemNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(ItemNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);
          ItemNetworkable();

    // members with updateStateMask. 52-byte stride (`52 * slot`).
    // The position sits at +0x08 here, not +0x0C as in Spring/FreeCamera.
    struct Record
    {
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ Vec3  position;                 // the transform translation
        /* +0x14 */ Quat  rotation;                // init: identity; otherwise only copied
        /* +0x24 */ int   ownerId;                  // world::Item::getOwnerId()
        /* +0x28 */ int   itemType;                 // world::Item::getItemType()
        /* +0x2C */ bool  enabled;                  // byte in both ports; followed by alignment padding
        /* +0x30 */ int   team;                     // world::Item::getTeam()
    };
    /* +0x50 */ int     m_refCount;
    /* +0x54 */ Record* m_records;                  // [4]
          virtual void predict(float);                              // ELF 5 B / PE ret 4: recovered empty
    static io::RegistredNetworkStats m_stats;         // "ItemObjectNetworkable", (140,140,190)
};

// Gives a kit a network identity without transmitting incremental state.
class KitNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(KitNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          KitNetworkable();
          virtual void addedToManager();                            // unique to this class
          virtual void removedFromManager();                        // unique to this class
          virtual float getAverageUpdateDeltaTime() const;
          virtual void predict(float);                              // — empty stub

    // [CHOSEN] a stride carrier only. getAverageUpdateDeltaTime indexes m_baselines at a 40-byte
    // stride and reads the shared +0x00 time header; nothing in this class ever allocates or writes
    // a record (updateStateMask is empty), so only the stride and the one field are attested.
    struct BaseLine
    {
        /* +0x00 */ float         time;
        /* +0x04 */ unsigned char pad04[36];            // [UNRESOLVED] - never touched
    };

    static io::RegistredNetworkStats m_stats;           // "KitObjectNetworkable" - NOT "Kit..."; see .cpp

    /* +0x50 */ int       m_refCount;                   // create's inlined ctor: 1
    /* +0x54 */ BaseLine* m_baselines;                  // create: 0 - and it stays null; see BaseLine
};

// Overhead-camera replication and prediction.
class OverheadCameraNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(OverheadCameraNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);
          OverheadCameraNetworkable();

    // Four-entry state ring, exact 40-byte stride (`40 * slot`) in both shipped x86 ports.
    struct Record
    {
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;                   // untouched by init and this class's wire
        /* +0x08 */ int   pad08;                    // untouched; Vec3 begins at +0x0C
        /* +0x0C */ Vec3  position;                 // the transform translation
        /* +0x18 */ float panX;                     // camera m_panState[0], bit 0
        /* +0x1C */ float panZ;                     // camera m_panState[2], also bit 0
        /* +0x20 */ int   defaultTeam;               // camera m_defaultTeam, bit 2
        /* +0x24 */ bool  enabled;                   // OverheadCamera::m_enabled, bit 3
    };
    /* +0x50 */ int     m_refCount;
    /* +0x54 */ Record* m_records;                  // [4]
          virtual bool getBaseLine(io::BaseLineData&);
          virtual void predict(float);
          bool findEmbracingIndices(float time, unsigned int* lo,
                                    unsigned int* hi) const;

    static io::RegistredNetworkStats m_stats;       // (180,180,180)
};

#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(OverheadCameraNetworkable::Record) == 0x28,
              "OverheadCameraNetworkable::Record x86 layout drift");
static_assert(sizeof(OverheadCameraNetworkable) == 0x58,
              "OverheadCameraNetworkable x86 layout drift");
#endif

// Controlled-object replication with a NetworkableObjectBase secondary interface.
class PlayerControlObjectNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(PlayerControlObjectNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          PlayerControlObjectNetworkable();

    // members with updateStateMask. 36-byte stride (`36 * slot`).
    struct Record
    {
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ int   weaponIndex[3];           // one index per weapon slot; -1 = none
        /* +0x14 */ char  _gap14[0x0C];             // [UNRESOLVED] - untouched by updateStateMask
        // All three images store and consume this as one byte: ELF 0x823E9AD/0x823E83C,
        // dedicated PE 0x5E7DC9/0x5E7F3E, client PE 0x635D69/0x635EDE. The trailing three bytes
        // still belong to the retail nine-dword record copy and are deliberately left unwritten.
        /* +0x20 */ bool  isDucking;                // IPlayerControlObject::getIsDucking()
        /* +0x21 */ char  _pad21[3];
    };
    /* +0x50 */ int     m_refCount;
    /* +0x54 */ Record* m_records;                  // [4]
          virtual unsigned int getGhostStateMask() const;
          virtual void predict(float);                              // — empty stub

    static io::RegistredNetworkStats m_stats;       // "PlayerControlObjectNetworkable"
};

#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(PlayerControlObjectNetworkable::Record) == 0x24,
              "PlayerControlObjectNetworkable::Record x86 layout drift");
static_assert(sizeof(PlayerControlObjectNetworkable) == 0x58,
              "PlayerControlObjectNetworkable x86 layout drift");
#endif

// Rotational state replication.
class RotationalBundleNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(RotationalBundleNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          RotationalBundleNetworkable();

    // members with updateStateMask. 48-byte stride (`48 * slot`).
    // Rotation is processed first but stored second - the record order is not the code order.
    struct Record
    {
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ float speed[3];                 // bundle +0x14C/+0x150/+0x154 - bits 3..5
        /* +0x14 */ float rotation[3];              // bundle +0x140/+0x144/+0x148 - bits 0..2
        /* +0x20 */ float excessInput;              // bundle +0x168 - bit 6
        // Applied to the bundle's trailing rotation fields; currently seeded with zeros.
        /* +0x24 */ float tail24[3];                // [UNRESOLVED] meaning
    };
    /* +0x50 */ int     m_refCount;
    // Allowed wire fields derived from mapped axes, automatic reset, and excess-input retention.
    /* +0x54 */ unsigned int m_localStateMask;
    /* +0x58 */ Record* m_records;                  // [4]
          virtual void reset();
          virtual unsigned int getGhostStateMask() const;           // 0x3F
          virtual bool getBaseLine(io::BaseLineData&);
          virtual void predict(float);                              // real body

          // predict's ring search: the two records whose times bracket t.
          bool findEmbracingIndices(float t, unsigned int* lo, unsigned int* hi) const;

    static io::RegistredNetworkStats m_stats;       // "RotationalBundleNetworkable"
};

// Simple-object transform, physics, and gameplay replication.
class SimpleObjectNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(SimpleObjectNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          SimpleObjectNetworkable();

    // Snapshot constructor initializes rotations to identity and leaves the tail uninitialized.
    struct UPDATE                                   // (0xBC)
    {
        UPDATE();                                   // identity quats, zeroed vectors

        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ float exactTime;                // stamped by setNetUpdate only when
                                                    // networkManager +0x5C is raised - a timing probe
        /* +0x0C */ Vec3  position;                 // transform translation - bit 1
        /* +0x18 */ Quat  rotation;                 // Quat::fromMat + full normalise - bit 0 (memcmp!)
        /* +0x28 */ Quat  quat28;                   // [UNRESOLVED] - identity from UPDATE(), nothing
                                                    // else touches it
        /* +0x38 */ Vec3  vec38;                    // physics +0x60 / setter +0x68 - bit 2. predict
                                                    // overwrites it with (pos delta / dt) velocity
        /* +0x44 */ Vec3  vec44;                    // physics +0xAC / +0x78 - bit 5
        /* +0x50 */ Vec3  vec50;                    // physics +0xB4 / +0x80 - bit 8
        /* +0x5C */ Vec3  vec5C;                    // physics +0x64 / +0x6C - bit 3
        /* +0x68 */ Vec3  vec68;                    // physics +0xB0 / +0x7C - bit 6
        /* +0x74 */ Vec3  vec74;                    // physics +0xB8 / +0x84 - bit 9
        /* +0x80 */ Vec3  vec80;                    // physics +0x18C / +0x188 - bit 14
        /* +0x8C */ Vec3  vec8C;                    // physics +0x194 / +0x190 - bit 14 again
        /* +0x98 */ int   hitPoints;                // (int)ceil(armor vslot +0x3C) - bit 4
        /* +0x9C */ bool  byte9C;                   // physics vslot +0x11C - bit 7
        /* +0x9D */ unsigned char replicationState; // object +0x134 - bit 10, TWO wire bits
        /* +0xA0 */ int   weaponIndex[3];           // bits 11 / 12 / 17; -1 = none
        /* +0xAC */ int   parachuteAC;              // Parachute object +0x3C4 - bit 13, range 0..7
        /* +0xB0 */ int   supplyB0;                 // SupplyObject +0x150 - bit 15, one wire bit
        /* +0xB4 */ int   pcoB4;                    // ipco vslot +0x14C / +0x150 - bit 16, range 0..15
        /* +0xB8 */ int   isDucking;                // ipco getIsDucking / setIsDucking - bit 18
    };

    /* +0x50 */ int     m_refCount;
    // by predict(): the prediction timestamp (now - interpolationTime),
    // stored every PMLinear pass. create() leaves it unwritten; nulled in the ctor here [CHOSEN].
    /* +0x54 */ float   m_predictTime;              // [INFERRED name]
    /* +0x58 */ UPDATE* m_records;                  // [4]
          virtual void reset();
          virtual unsigned int getGhostStateMask() const;           // 0x5849B
          virtual bool getBaseLine(io::BaseLineData&);
          virtual float getAverageUpdateDeltaTime() const;
          virtual void predict(float);                              // real body

          // the same ring search RotationalBundleNetworkable carries, over the
          // 188-byte stride.
          bool findEmbracingIndices(float t, unsigned int* lo, unsigned int* hi) const;
          void printInfo();                                         // m_next sync

    static io::RegistredNetworkStats m_stats;       // "SimpleObjectNetworkable"
};

// Replicates soldier movement, equipment and ragdoll state.
// Forwards owner-id changes to weapon network components.
class SoldierNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase   // , 20 slots
{ public: BF2_NETWORKABLE_CONTRACT(SoldierNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          SoldierNetworkable();

    // Snapshot ring element. Construction leaves +0x7C..+0x7F and +0x9C untouched until updated.
    struct UPDATE                                   // (0xA0)
    {
        UPDATE();                                   // zeroes all but the six

        /* +0x00 */ float time;                     // the shared header - milliseconds (setNetUpdate
                                                    // divides deltas by 1000 to get seconds)
        /* +0x04 */ unsigned char used;             // the valid/used flag (getUpdateAndMarkUsed)
        /* +0x08 */ Vec3  absoluteTrans;            // transform cols 12..14 - bit 0
        /* +0x14 */ Vec3  positionalSpeed;          // SoldierPhysicsNode - bit 7
        /* +0x20 */ Vec3  positionalAcceleration;   // PointPhysicsNode - bit 8 (full precision only)
        /* +0x2C */ Vec3  positionalFriction;       // SoldierPhysicsNode - bit 9 (full only)
        /* +0x38 */ Vec3  localLinearSpeed;         // PointPhysicsNode - bit 18 (full only)
        /* +0x44 */ float facingDegrees;            // soldier +0x1F0 m_facingDegrees - bit 1
        /* +0x48 */ float lookLeftRightAngle;       // soldier +0x20C - bit 2
        /* +0x4C */ float pendingTurn;              // soldier +0x214 (m_dword214) - bit 3; on a
                                                    // ladder overridden by -(camera rotZ)
        /* +0x50 */ float lookUpDownAngle;          // soldier +0x204 - bit 4
        /* +0x54 */ int   pose;                     // bit 5 (with requestedPose)
        /* +0x58 */ int   requestedPose;            // bit 5
        /* +0x5C */ int   hitPoints;                // ceil(max(hp,0))/maxHp*255, 0..255 - bit 6
        /* +0x60 */ int   lastDamageType;           // armor getLastDamageType - rides on bit 6
        /* +0x64 */ float smoothedForward;          // soldier +0x1F8 - bit 10 (/50 quantised)
        /* +0x68 */ float smoothedStrafe;           // soldier +0x1F4 - bit 11 (/50 quantised)
        /* +0x6C */ int   itemIndex;                // soldier m_activeItemIndex - bit 12; wire = +1
        /* +0x70 */ int   poseInputLatch;           // soldier +0x218 - bit 13, TWO wire bits
        /* +0x74 */ float currentAbility;           // getCurrentAbility - bit 17, full only
        /* +0x78 */ float sprintStamina;            // PCO getSprintStamina - bit 14
        /* +0x7C */ unsigned char isSprinting;      // ctor-skipped - bit 14, full only
        /* +0x7D */ unsigned char isSkyDiving;      // ctor-skipped - unconditional 1 bit
        /* +0x7E */ unsigned char isSwimming;       // ctor-skipped - unconditional 1 bit
        /* +0x7F */ unsigned char weaponStowed;     // ctor-skipped - soldier +0x38C - bit 12
        /* +0x80 */ int   weaponZoom;               // IWeaponObject getZoom - unconditional, 0..3
        /* +0x84 */ int   weaponState;              // IWeaponObject getWeaponState - unconditional, 0..1
        /* +0x88 */ unsigned int ladderId;          // bit 16, 0..255 on the wire
        /* +0x8C */ float ladderState;              // Ladder::getCurrentState - rides on bit 16, id != 0
        /* +0x90 */ unsigned int ziplineId;         // bit 20, 16 raw bits
        /* +0x94 */ float ziplineState;             // Zipline::getCurrentState - rides on bit 20, id != 0
        /* +0x98 */ unsigned char responseByteFA;   // SoldierResponsePhysics +0xFA - unconditional 1 bit
        // AN INT IN THE struct, whatever the receive path's float local suggests: the ctor
        // skips it, the sender masks it & 0xF as an int, the apply hands it to setAmmoStatus(int).
        /* +0x9C */ int   ammoStatus;               // ctor-skipped - getAmmoStatus & 0xF - bit 19
    };

    /* +0x50 */ int     m_refCount;
    /* +0x54 */ UPDATE* m_updates;                  // [4] - operator new[](0x280), init()
          virtual void reset();                                     // null ring
                                                                    // skips the base reset too
          virtual unsigned int getGhostStateMask() const;
          virtual bool getBaseLine(io::BaseLineData&);              // cursor test only
          virtual float getAverageUpdateDeltaTime() const;
          virtual void predict(float);                              // real body
          virtual void setOwnerID(int);                             // — unique among 16

          // The ring accessors the wire pair addresses records through (NetworkableBase.h's
          // "helper shape"; TriggerableTargetNetworkable carries the same two, non-virtual).
          UPDATE* getUpdate(unsigned int slot);
          UPDATE* getUpdateAndMarkUsed(unsigned int slot);
          bool    getIsUpdateValid(unsigned int slot);              // no null guard
          // the house ring search (RotationalBundle/SimpleObject carry it too).
          bool    findEmbracingIndices(float t, unsigned int* lo, unsigned int* hi) const;
          // dice'S OWN name: soldier updateTransformation + updateCameraTransformation.
          void    setSoldierStateHACK();
          // predict's apply: update2's pose/look onto the soldier, stamina and
          // ladder/zipline state lerped between update1 and update2 when update1 is non-null.
          void    setPredictedState(world::Soldier* soldier, const UPDATE* update1,
                                    const UPDATE* update2, float fraction);

    static io::RegistredNetworkStats m_stats;       // "SoldierNetworkable", (250, 0, 0)
};

// Spring-state replication.
class SpringNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(SpringNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);     // the factory
          SpringNetworkable();

    // members with updateStateMask. 40-byte stride (`40 * slot`).
    struct BaseLine
    {
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ bool  byte04;
        /* +0x08 */ int   pad08;                    // [UNRESOLVED] - the Vec3 starts at +0x0C
        /* +0x0C */ Vec3  position;                 // the transform's translation - diffed
        /* +0x18 */ Vec3  springVector;             // spring +0x240 - snapshotted, never diffed
        /* +0x24 */ float trailing;                 // spring +0x250 or 0 - snapshotted, never diffed
    };
    /* +0x50 */ int       m_refCount;
    /* +0x54 */ bool      m_flag54;                 // by reset(): "the spring's
                                                    // engine is type-bit-7 AND the +0x48 comp answers
                                                    // bit 3" - the trailing float's precondition
    /* +0x58 */ BaseLine* m_baselines;              // [4]
          virtual void reset();
          virtual unsigned int getGhostStateMask() const;           // 0
          virtual void predict(float);                              // — empty stub

          static void printSpringInfo(world::PhysicsSpring*);       // empty
          void dumpState(int);                                      // empty frame

    static io::RegistredNetworkStats m_stats;       // "SpringNetworkable", (50,50,50)
    // .data 0x89892A4 = 1 - setNetUpdate's apply-arm gate ("the spring fix"), ON in retail.
    static int globalUseSpringFix;
};

// Target-object replication uses typed access because field and virtual offsets differ by platform.
class TargetObjectNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(TargetObjectNetworkable);
          static IBase* create(unsigned int iid, IBase* outer);
          TargetObjectNetworkable();

    // members with updateStateMask. 24-byte stride (`24 * slot`).
    struct BaseLine
    {
        /* +0x00 */ float         time;             // the shared header
        /* +0x04 */ bool          byte04;
        /* +0x08 */ Vec3          position;         // the transform's translation
        /* +0x14 */ unsigned char targetActive;     // TargetObject::m_targetActive low byte
        /* +0x15 */ unsigned char targetPlayerId;   // TargetObject::m_targetPlayerId low byte
    };
    /* +0x50 */ int             m_refCount;
    /* +0x54 */ BaseLine*       m_baselines;        // [4]
    /* +0x58 */ world::TargetObject* m_targetObject;
          virtual void predict(float);                              // — empty stub

    static io::RegistredNetworkStats m_stats;       // (180,240,240)
};

#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(TargetObjectNetworkable::BaseLine) == 0x18,
              "TargetObjectNetworkable::BaseLine x86 layout drift");
static_assert(sizeof(TargetObjectNetworkable) == 0x5C,
              "TargetObjectNetworkable x86 layout drift");
#endif

// second base io::NetworkableObjectBase sits at +0x3C in all sixteen.
class TriggerableTargetNetworkable : public io::NetworkableBase, public io::NetworkableObjectBase
{ public: BF2_NETWORKABLE_CONTRACT(TriggerableTargetNetworkable);
          TriggerableTargetNetworkable();
          static IBase* create(unsigned int iid, IBase* outer);
          bool getIsUpdateValid(unsigned int slot);
          static io::RegistredNetworkStats m_stats;

    // Comparisons use the previous record only after getUpdateAndMarkUsed marks it valid.
    struct Record
    {
        Record();                                    // ELF824D850: zero all logical fields
        /* +0x00 */ float time;                     // the shared header
        /* +0x04 */ unsigned char used;             // the valid flag - see above
        /* +0x08 */ float position[3];              // the transform translation - bit 0
        /* +0x14 */ float rotation[3];              // getRotationZXY, negated+reversed - bit 1
        /* +0x20 */ int   field14C;                 // target motion state, ELF +14C / PE +17C, bit2
        /* +0x24 */ int   field154;                 // cycles remaining, ELF +154 / PE +184, bit3
    };
    Record* getUpdate(unsigned int slot);
    Record* getUpdateAndMarkUsed(unsigned int slot);
    /* +0x50 */ int     m_refCount;
    /* +0x54 */ Record* m_records;                  // [4]
          virtual unsigned int getGhostStateMask() const;
          virtual void predict(float);                              // , shipped empty body
};

} // namespace hfe
} // namespace dice

#endif // IO_NETWORK_NETWORKABLES_H
