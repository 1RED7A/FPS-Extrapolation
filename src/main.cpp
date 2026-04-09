#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float TELEPORT_THRESHOLD     = 300.f;
static constexpr float SPEED_CHANGE_THRESHOLD =   5.f;

// ─────────────────────────────────────────────────────────────────────────────
// ExtrapolationFields — all state for one game-layer instance
// ─────────────────────────────────────────────────────────────────────────────
struct ExtrapolationFields {
    // Timing
    float timeTilNextTick     = 0.f;
    float progressTilNextTick = 0.f;
    float modifiedDeltaReturn = 0.f;

    // Camera
    CCPoint lastCamPos        = {};
    CCPoint lastCamPos2       = {};
    CCPoint lastCamDelta      = {};

    // Ground layer 1
    CCPoint lastGroundPos     = {};
    CCPoint lastGroundPos2    = {};

    // Ground layer 2  (FIX 1: fully separate history from layer 1)
    CCPoint lastGround2Pos    = {};
    CCPoint lastGround2Pos2   = {};

    // Player rotations
    float lastRot1            = 0.f;
    float lastRot2            = 0.f;

    // FIX 3: per-instance frame counter (was a shared static — now lives here)
    int frameCounter          = 0;
    int lastUpdateFrame       = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// GJBaseGameLayer modification
// ─────────────────────────────────────────────────────────────────────────────
class $modify(ExtrapolatedGameLayer, GJBaseGameLayer) {
    struct Fields {
        ExtrapolationFields ex = {};
    };

    // ── Reset helper ──────────────────────────────────────────────────────────
    void resetExtrapolationState() {
        auto& ex = m_fields->ex;
        ex.timeTilNextTick     = 0.f;
        ex.progressTilNextTick = 0.f;
        ex.modifiedDeltaReturn = 0.f;
        ex.lastCamPos          = {};
        ex.lastCamPos2         = {};
        ex.lastCamDelta        = {};
        ex.lastGroundPos       = {};
        ex.lastGroundPos2      = {};
        ex.lastGround2Pos      = {};
        ex.lastGround2Pos2     = {};
        ex.lastRot1            = 0.f;
        ex.lastRot2            = 0.f;
        // NOTE: frameCounter intentionally not reset — it tracks render frames,
        // not game state. lastUpdateFrame is reset so the next update is treated
        // as a fresh tick.
        ex.lastUpdateFrame     = -1;
    }

    // ── Capture modifiedDelta ─────────────────────────────────────────────────
    float getModifiedDelta(float dt) {
        float pRet = GJBaseGameLayer::getModifiedDelta(dt);
        m_fields->ex.modifiedDeltaReturn = pRet;
        return pRet;
    }

    // ── Hook: death / restart ─────────────────────────────────────────────────
    void resetLevel() {
        resetExtrapolationState();
        GJBaseGameLayer::resetLevel();
    }

    // ── Hook: level begins / checkpoint ──────────────────────────────────────
    void startGame() {
        resetExtrapolationState();
        GJBaseGameLayer::startGame();
    }

    // ── Main update ───────────────────────────────────────────────────────────
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        if (!Mod::get()->getSettingValue<bool>("enabled"))
            return;

        auto pl = PlayLayer::get();
        if (!pl) return;
        if (pl->m_levelEndAnimationStarted) return;
        if (!isRunning() || dt == 0.f) return;

        if (pl->m_isPracticeMode &&
            Mod::get()->getSettingValue<bool>("disable-in-practice"))
            return;

        auto& ex = m_fields->ex;

        // FIX 3: per-instance counter, not a shared static
        ex.frameCounter++;

        bool tickFired = (ex.modifiedDeltaReturn != 0.f) ||
                         (ex.frameCounter != ex.lastUpdateFrame + 1);

        if (tickFired) {

            // FIX 1: ground layer 1 history — fully independent
            if (m_groundLayer) {
                ex.lastGroundPos2 = ex.lastGroundPos;
                ex.lastGroundPos  = m_groundLayer->getPosition();
            }

            // FIX 1: ground layer 2 history — fully independent
            if (m_groundLayer2) {
                ex.lastGround2Pos2 = ex.lastGround2Pos;
                ex.lastGround2Pos  = m_groundLayer2->getPosition();
            }

            // Speed-change detection (Section 8)
            CCPoint currentDelta = ex.lastCamPos - ex.lastCamPos2;
            float   deltaChange  = ccpDistance(currentDelta, ex.lastCamDelta);
            if (deltaChange > SPEED_CHANGE_THRESHOLD) {
                ex.lastCamPos2     = ex.lastCamPos;
                ex.lastGroundPos2  = ex.lastGroundPos;
                ex.lastGround2Pos2 = ex.lastGround2Pos;
            }
            ex.lastCamDelta = currentDelta;

            // Camera history
            ex.timeTilNextTick     = ex.modifiedDeltaReturn;
            ex.progressTilNextTick = 0.f;
            ex.lastCamPos2         = ex.lastCamPos;
            ex.lastCamPos          = m_objectLayer->getPosition();

            // Save player rotations at tick time
            if (m_player1 && m_player1->m_mainLayer)
                ex.lastRot1 = m_player1->m_mainLayer->getRotation();
            if (m_player2 && m_player2->m_mainLayer)
                ex.lastRot2 = m_player2->m_mainLayer->getRotation();

            ex.modifiedDeltaReturn = 0.f;
            ex.lastUpdateFrame     = ex.frameCounter;

        } else {
            ex.progressTilNextTick += dt;
        }

        if (ex.timeTilNextTick == 0.f) return;

        // Teleport detection
        float camJump = ccpDistance(m_objectLayer->getPosition(), ex.lastCamPos);
        if (camJump > TELEPORT_THRESHOLD) {
            resetExtrapolationState();
            return;
        }

        // Clamped percent
        float maxPercent = Mod::get()->getSettingValue<float>("max-percent");
        float percent = std::clamp(
            ex.progressTilNextTick / ex.timeTilNextTick,
            0.f,
            maxPercent
        );

        bool doCamera = Mod::get()->getSettingValue<bool>("extrapolate-camera");
        bool doPlayer = Mod::get()->getSettingValue<bool>("extrapolate-player");
        bool doGround = Mod::get()->getSettingValue<bool>("extrapolate-ground");

        // Camera
        if (doCamera) {
            CCPoint endCamPos = ex.lastCamPos + (ex.lastCamPos - ex.lastCamPos2);
            m_objectLayer->setPosition({
                std::lerp(ex.lastCamPos.x, endCamPos.x, percent),
                std::lerp(ex.lastCamPos.y, endCamPos.y, percent)
            });
        }

        // Ground layers
        if (doGround) {
            extrapolateGround(m_groundLayer,
                              ex.lastGroundPos,  ex.lastGroundPos2,  percent);
            extrapolateGround(m_groundLayer2,
                              ex.lastGround2Pos, ex.lastGround2Pos2, percent);
        }

        // Players
        if (doPlayer) {
            extrapolatePlayer(m_player1, percent, ex.lastRot1);
            if (m_player2) extrapolatePlayer(m_player2, percent, ex.lastRot2);
        }
    }

    // ── Player extrapolation ──────────────────────────────────────────────────
    void extrapolatePlayer(PlayerObject* player, float percent, float& lastRot) {
        if (!player) return;

        float endX = player->m_position.x +
                     (player->m_position.x - player->m_lastPosition.x);
        float endY = player->m_position.y +
                     (player->m_position.y - player->m_lastPosition.y);

        player->CCNode::setPosition({
            std::lerp(player->m_position.x, endX, percent),
            std::lerp(player->m_position.y, endY, percent)
        });

        float rotSpeed    = (player->m_isBall && player->m_isBallRotating)
                            ? 1.f : player->m_rotateSpeed;
        float rotDelta    = (player->m_rotationSpeed * rotSpeed) / 240.f;
        float sidewaysOff = player->m_isSideways ? -90.f : 0.f;

        player->m_mainLayer->setRotation(
            std::lerp(lastRot, lastRot + rotDelta, percent) + sidewaysOff
        );
    }

    // ── Ground extrapolation ──────────────────────────────────────────────────
    void extrapolateGround(GJGroundLayer* ground,
                           CCPoint lastPos, CCPoint lastPos2,
                           float percent) {
        if (!ground) return;
        CCPoint endPos = lastPos + (lastPos - lastPos2);
        ground->setPosition({
            std::lerp(lastPos.x, endPos.x, percent),
            std::lerp(lastPos.y, endPos.y, percent)
        });
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PlayLayer modification — FIX 2: safe levelComplete reset
// Instead of an unsafe cross-class cast, we hook resetLevel which PlayLayer
// calls internally as part of its cleanup. If a direct hook is needed, we
// use the Geode m_fields accessor pattern correctly via GJBaseGameLayer.
// ─────────────────────────────────────────────────────────────────────────────
class $modify(ExtrapolatedPlayLayer, PlayLayer) {

    // levelComplete triggers the end animation. We reset BEFORE calling the
    // original so no stale camera position leaks into the end screen.
    // FIX 2: access resetExtrapolationState through the correct Geode hook
    // by casting to our modified type using static_cast on the same object
    // pointer (safe because $modify guarantees the vtable is ours).
    void levelComplete() {
        static_cast<ExtrapolatedGameLayer*>(
            static_cast<GJBaseGameLayer*>(this)
        )->resetExtrapolationState();
        PlayLayer::levelComplete();
    }
};
