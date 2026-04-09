#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

struct ExtrapolationFields {
    float timeTilNextTick = 0.f;
    float progressTilNextTick = 0.f;
    CCPoint lastCamPos2 = {};
    CCPoint lastCamPos = {};
    float modifiedDeltaReturn = 0.f;
};

class $modify(ExtrapolatedGameLayer, GJBaseGameLayer) {
    struct Fields {
        ExtrapolationFields ex = {};
    };

    float getModifiedDelta(float dt) {
        float pRet = GJBaseGameLayer::getModifiedDelta(dt);
        m_fields->ex.modifiedDeltaReturn = pRet;
        return pRet;
    }

    void update(float dt) {
        GJBaseGameLayer::update(dt);

        if (!Mod::get()->getSettingValue<bool>("enabled"))
            return;

        auto pl = PlayLayer::get();
        if (!pl) return;
        if (pl->m_levelEndAnimationStarted) return;
        if (!isRunning() || dt == 0) return;

        auto& ex = m_fields->ex;

        if (ex.modifiedDeltaReturn != 0.f) {
            ex.timeTilNextTick = ex.modifiedDeltaReturn;
            ex.progressTilNextTick = 0.f;
            ex.lastCamPos2 = ex.lastCamPos;
            ex.lastCamPos = m_objectLayer->getPosition();
        } else {
            ex.progressTilNextTick += dt;
        }

        if (ex.timeTilNextTick == 0.f) return;

        float percent = ex.progressTilNextTick / ex.timeTilNextTick;

        // Extrapolate camera
        CCPoint endCamPos = ex.lastCamPos + (ex.lastCamPos - ex.lastCamPos2);
        m_objectLayer->setPosition({
            (float)std::lerp((double)ex.lastCamPos.x, (double)endCamPos.x, (double)percent),
            (float)std::lerp((double)ex.lastCamPos.y, (double)endCamPos.y, (double)percent)
        });

        // Extrapolate ground layers
        extrapolateGround(m_groundLayer, percent);
        extrapolateGround(m_groundLayer2, percent);

        // Extrapolate players
        extrapolatePlayer(m_player1, percent);
        if (m_player2) extrapolatePlayer(m_player2, percent);
    }

    void extrapolatePlayer(PlayerObject* player, float percent) {
        if (!player) return;

        float endX = player->m_position.x + (player->m_position.x - player->m_lastPosition.x);
        float endY = player->m_position.y + (player->m_position.y - player->m_lastPosition.y);

        float rotSpeed = (player->m_isBall && player->m_isBallRotating)
            ? 1.f
            : player->m_rotateSpeed;
        float endRot = (player->m_rotationSpeed * rotSpeed) / 240.f;

        float sidewaysOffset = player->m_isSideways ? -90.f : 0.f;

        player->CCNode::setPosition({
            (float)std::lerp((double)player->m_position.x, (double)endX, (double)percent),
            (float)std::lerp((double)player->m_position.y, (double)endY, (double)percent)
        });
        player->m_mainLayer->setRotation(
            std::lerp(0.f, endRot, percent) + sidewaysOffset
        );
    }

    void extrapolateGround(GJGroundLayer* ground, float percent) {
        if (!ground) return;
        auto& ex = m_fields->ex;

        float moveBy = ex.lastCamPos.x - ex.lastCamPos2.x;

        for (auto child : CCArrayExt<CCNode*>(ground->getChildren())) {
            if (typeinfo_cast<CCSpriteBatchNode*>(child)) {
                child->setPositionX(
                    (float)std::lerp(0.0, (double)moveBy, (double)percent)
                );
            }
        }
    }
};
