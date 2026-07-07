#include "views/magic_view.hpp"

#include "assets/assets.h"
#include <box2d/box2d.h>
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>

namespace files {
namespace {

constexpr int32_t kScreenWidth   = 320;
constexpr int32_t kScreenHeight  = 170;
constexpr int32_t kMagicSize     = 10;
constexpr int32_t kBowWidth      = 6;
constexpr int32_t kBowHeight     = 38;
constexpr int32_t kTargetSize    = 14;
constexpr int32_t kBrickWidth    = 26;
constexpr int32_t kBrickHeight   = 4;
constexpr size_t kTargetCount    = 7;
constexpr size_t kBrickCount     = 15;
constexpr size_t kPuffDotCount   = 5;
constexpr uint32_t kIdleMs       = 2000;
constexpr uint32_t kPullMs       = 900;
constexpr uint32_t kAngleMs      = 1350;
constexpr uint32_t kLaunchMs     = kIdleMs + kAngleMs;
constexpr uint32_t kMaskFadeMs   = 420;
constexpr uint32_t kLifeMs       = 8000;
constexpr uint32_t kPuffLifeMs   = 460;
constexpr float kPixelsPerMeter  = 40.0f;
constexpr float kPhysicsStep     = 1.0f / 60.0f;
constexpr float kMaxCatchup      = 1.0f / 18.0f;
constexpr float kMagicRadius     = kMagicSize / 2.0f;
constexpr float kHitPopSpeed     = 2.55f;
constexpr float kPi              = 3.14159265358979323846f;
constexpr uint32_t kMaskColor    = 0x000000;
constexpr lv_opa_t kMaskOpacity  = 217;
constexpr uint32_t kStringColor  = 0x765C8D;
constexpr uint32_t kPuffColors[] = {0x7FE36A, 0xB7F56C, 0xF0F28A};

float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

float length(float x, float y)
{
    return std::sqrt(x * x + y * y);
}

lv_opa_t opaFromProgress(float progress)
{
    return static_cast<lv_opa_t>(
        std::clamp(static_cast<int>(std::round(progress * LV_OPA_COVER)), 0, static_cast<int>(LV_OPA_COVER)));
}

float toMeters(float pixels)
{
    return pixels / kPixelsPerMeter;
}

float toPixels(float meters)
{
    return meters * kPixelsPerMeter;
}

b2Vec2 toWorldPoint(float x, float y)
{
    return {toMeters(x), toMeters(y)};
}

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct BrickPlacement {
    float x       = 0.0f;
    float y       = 0.0f;
    bool vertical = false;
};

struct MagicLayout {
    std::array<Point, kTargetCount> targets{};
    size_t targetCount = 0;
    std::array<BrickPlacement, kBrickCount> bricks{};
    size_t brickCount = 0;
};

constexpr std::array<MagicLayout, 3> kLayouts = {
    MagicLayout{
        {Point{241.0f, 126.0f}, Point{269.0f, 126.0f}, Point{241.0f, 156.0f}, Point{269.0f, 156.0f},
         Point{299.0f, 156.0f}},
        5,
        {BrickPlacement{221.0f, 125.0f, true}, BrickPlacement{249.0f, 125.0f, true},
         BrickPlacement{277.0f, 125.0f, true}, BrickPlacement{221.0f, 155.0f, true},
         BrickPlacement{249.0f, 155.0f, true}, BrickPlacement{277.0f, 155.0f, true},
         BrickPlacement{235.0f, 110.0f, false}, BrickPlacement{263.0f, 110.0f, false},
         BrickPlacement{235.0f, 140.0f, false}, BrickPlacement{263.0f, 140.0f, false}},
        10,
    },
    MagicLayout{
        {Point{236.0f, 36.0f}, Point{236.0f, 66.0f}, Point{236.0f, 96.0f}, Point{236.0f, 126.0f},
         Point{236.0f, 156.0f}},
        5,
        {BrickPlacement{218.0f, 65.0f, true}, BrickPlacement{243.0f, 65.0f, true}, BrickPlacement{218.0f, 95.0f, true},
         BrickPlacement{243.0f, 95.0f, true}, BrickPlacement{218.0f, 125.0f, true},
         BrickPlacement{243.0f, 125.0f, true}, BrickPlacement{218.0f, 155.0f, true},
         BrickPlacement{243.0f, 155.0f, true}, BrickPlacement{230.0f, 50.0f, false},
         BrickPlacement{230.0f, 80.0f, false}, BrickPlacement{230.0f, 110.0f, false},
         BrickPlacement{230.0f, 140.0f, false}},
        12,
    },
    MagicLayout{
        {Point{207.0f, 156.0f}, Point{233.0f, 156.0f}, Point{259.0f, 156.0f}, Point{220.0f, 126.0f},
         Point{246.0f, 126.0f}, Point{233.0f, 96.0f}, Point{235.0f, 66.0f}},
        7,
        {BrickPlacement{187.0f, 155.0f, true}, BrickPlacement{212.0f, 155.0f, true},
         BrickPlacement{238.0f, 155.0f, true}, BrickPlacement{264.0f, 155.0f, true},
         BrickPlacement{200.0f, 125.0f, true}, BrickPlacement{226.0f, 125.0f, true},
         BrickPlacement{251.0f, 125.0f, true}, BrickPlacement{213.0f, 95.0f, true}, BrickPlacement{239.0f, 95.0f, true},
         BrickPlacement{199.0f, 140.0f, false}, BrickPlacement{225.0f, 140.0f, false},
         BrickPlacement{251.0f, 140.0f, false}, BrickPlacement{212.0f, 110.0f, false},
         BrickPlacement{238.0f, 110.0f, false}, BrickPlacement{225.0f, 80.0f, false}},
        15,
    },
};

Point toScreenPoint(b2Vec2 point)
{
    return {toPixels(point.x), toPixels(point.y)};
}

Point rotatePoint(const Point& point, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

struct LineObject {
    explicit LineObject(lv_obj_t* parent)
    {
        obj = lv_line_create(parent);
        lv_obj_set_size(obj, kScreenWidth, kScreenHeight);
        lv_obj_set_pos(obj, 0, 0);
        lv_obj_set_style_line_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_line_color(obj, lv_color_hex(kStringColor), LV_PART_MAIN);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    ~LineObject()
    {
        if (obj && lv_obj_is_valid(obj)) {
            lv_obj_delete(obj);
        }
    }

    void setHidden(bool hidden)
    {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void setPoints(const Point& from, const Point& to)
    {
        points[0] = {static_cast<lv_coord_t>(std::round(from.x)), static_cast<lv_coord_t>(std::round(from.y))};
        points[1] = {static_cast<lv_coord_t>(std::round(to.x)), static_cast<lv_coord_t>(std::round(to.y))};
        lv_line_set_points(obj, points.data(), static_cast<uint16_t>(points.size()));
    }

    void moveForeground()
    {
        lv_obj_move_foreground(obj);
    }

    void setOpa(lv_opa_t opa)
    {
        lv_obj_set_style_line_opa(obj, opa, LV_PART_MAIN);
    }

    lv_obj_t* obj = nullptr;
    std::array<lv_point_precise_t, 2> points{};
};

}  // namespace

struct MagicView::Impl {
    enum class BodyKind { Target, Brick };

    struct Body {
        ~Body()
        {
            for (auto* dot : puffDots) {
                if (dot && lv_obj_is_valid(dot)) {
                    lv_obj_delete(dot);
                }
            }
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> image;
        std::array<lv_obj_t*, kPuffDotCount> puffDots{};
        b2BodyId bodyId   = b2_nullBodyId;
        b2ShapeId shapeId = b2_nullShapeId;
        BodyKind kind     = BodyKind::Brick;
        float imageWidth  = 0.0f;
        float imageHeight = 0.0f;
        float imagePivotX = 0.0f;
        float imagePivotY = 0.0f;
        Point puffCenter;
        uint32_t puffStartMs = 0;
        uint32_t puffSeed    = 0;
        bool alive           = false;
        bool puffActive      = false;
    };

    explicit Impl(lv_obj_t* parent) : parent(parent), topString(parent), bottomString(parent)
    {
        mask = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        mask->setSize(kScreenWidth, kScreenHeight);
        mask->setPos(0, 0);
        mask->setBgColor(lv_color_hex(kMaskColor));
        mask->setBgOpa(kMaskOpacity);
        mask->setBorderWidth(0);
        mask->setPaddingAll(0);
        mask->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        mask->addFlag(LV_OBJ_FLAG_HIDDEN);
        mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        bow = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent);
        bow->setSrc(&image_magic_bow);
        bow->setPivot(kBowWidth / 2, kBowHeight / 2);
        bow->setHidden(true);
        bow->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        magic = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent);
        magic->setSrc(&image_magic);
        magic->setPivot(kMagicSize / 2, kMagicSize / 2);
        magic->setHidden(true);
        magic->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        for (auto& target : targets) {
            initBodyImage(target, BodyKind::Target, &image_magic_target, kTargetSize, kTargetSize);
        }
        for (auto& brick : bricks) {
            initBodyImage(brick, BodyKind::Brick, &image_magic_brick, kBrickWidth, kBrickHeight);
        }
    }

    ~Impl()
    {
        destroyWorld();
    }

    lv_obj_t* parent = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> mask;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> bow;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> magic;
    LineObject topString;
    LineObject bottomString;
    std::array<Body, kTargetCount> targets;
    std::array<Body, kBrickCount> bricks;
    b2WorldId worldId      = b2_nullWorldId;
    b2BodyId magicBodyId   = b2_nullBodyId;
    b2ShapeId magicShapeId = b2_nullShapeId;
    Point magicPos;
    Point plannedMagicVel;
    Point launchPos;
    Point aimPos;
    Point restPos;
    Point bowCenter;
    Point launchDir;
    uint32_t startMs       = 0;
    uint32_t lastTickMs    = 0;
    uint32_t serial        = 0;
    float physicsRemainder = 0.0f;
    bool active            = false;
    bool launched          = false;
    bool magicVisible      = false;
    bool structureAwake    = false;
    int32_t bowX           = 37;
    int32_t bowY           = 86;
    float bowAngle         = 0.0f;
    float targetBowAngle   = 0.0f;

    void initBodyImage(Body& body, BodyKind kind, const lv_image_dsc_t* src, float imageWidth, float imageHeight)
    {
        body.kind  = kind;
        body.image = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent);
        body.image->setSrc(src);
        body.image->setPivot(static_cast<int32_t>(std::round(imageWidth / 2.0f)),
                             static_cast<int32_t>(std::round(imageHeight / 2.0f)));
        body.image->setHidden(true);
        body.image->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        body.imageWidth  = imageWidth;
        body.imageHeight = imageHeight;
        body.imagePivotX = imageWidth / 2.0f;
        body.imagePivotY = imageHeight / 2.0f;

        if (kind != BodyKind::Target) {
            return;
        }

        for (size_t i = 0; i < body.puffDots.size(); ++i) {
            auto* dot = lv_obj_create(parent);
            lv_obj_set_size(dot, 3, 3);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(dot, lv_color_hex(kPuffColors[i % std::size(kPuffColors)]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            body.puffDots[i] = dot;
        }
    }

    void generate(uint32_t magicSerial)
    {
        if (magicSerial == 0 || !parent || !magic || !bow) {
            return;
        }

        serial           = magicSerial;
        active           = true;
        launched         = false;
        structureAwake   = false;
        startMs          = lv_tick_get();
        lastTickMs       = startMs;
        physicsRemainder = 0.0f;
        destroyWorld();
        createWorld();

        std::mt19937 rng(0xF11E500Du ^ (magicSerial * 0x9E3779B9u));
        std::uniform_real_distribution<float> pullDist(18.0f, 25.0f);
        std::uniform_real_distribution<float> vxDist(315.0f, 365.0f);
        std::uniform_real_distribution<float> vyDist(-310.0f, -245.0f);
        std::uniform_int_distribution<int32_t> pileXDist(-3, 4);
        std::uniform_int_distribution<size_t> layoutDist(0, kLayouts.size() - 1);

        plannedMagicVel  = {vxDist(rng), vyDist(rng)};
        const float len  = std::max(1.0f, length(plannedMagicVel.x, plannedMagicVel.y));
        launchDir        = {plannedMagicVel.x / len, plannedMagicVel.y / len};
        targetBowAngle   = std::atan2(launchDir.y, launchDir.x);
        bowAngle         = 0.0f;
        bowCenter        = {48.0f + static_cast<float>(magicSerial % 3), 141.0f};
        bowX             = static_cast<int32_t>(std::round(bowCenter.x - kBowWidth / 2.0f));
        bowY             = static_cast<int32_t>(std::round(bowCenter.y - kBowHeight / 2.0f));
        const float pull = pullDist(rng);
        aimPos    = {bowCenter.x - launchDir.x * pull - kMagicRadius, bowCenter.y - launchDir.y * pull - kMagicRadius};
        restPos   = {bowCenter.x + launchDir.x * 5.0f - kMagicRadius, bowCenter.y + launchDir.y * 5.0f - kMagicRadius};
        launchPos = aimPos;
        magicPos  = restPos;

        const int32_t xJitter     = pileXDist(rng);
        const int32_t yJitter     = 0;
        const MagicLayout& layout = kLayouts[layoutDist(rng)];
        setupTargets(layout, xJitter, yJitter);
        setupBricks(layout, xJitter, yJitter);

        mask->removeFlag(LV_OBJ_FLAG_HIDDEN);
        mask->setBgOpa(0);
        lv_obj_move_foreground(mask->raw_ptr());

        bow->setHidden(false);
        bow->setPos(bowX, bowY);
        bow->setRotation(0);
        lv_obj_move_foreground(bow->raw_ptr());

        magic->setHidden(false);
        magicVisible = true;
        lv_obj_move_foreground(magic->raw_ptr());

        showStrings(false);
        moveAllForeground();
        applyOverlayOpacity(0);
        applyState();
    }

    void createWorld()
    {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity    = {0.0f, 18.5f};
        worldId             = b2CreateWorld(&worldDef);
        b2World_SetHitEventThreshold(worldId, kHitPopSpeed);
        b2World_SetContactTuning(worldId, 120.0f, 0.55f, 7.5f);
        createBounds();
    }

    void destroyWorld()
    {
        if (b2World_IsValid(worldId)) {
            b2DestroyWorld(worldId);
        }
        worldId        = b2_nullWorldId;
        magicBodyId    = b2_nullBodyId;
        magicShapeId   = b2_nullShapeId;
        structureAwake = false;
        for (auto& target : targets) {
            target.bodyId  = b2_nullBodyId;
            target.shapeId = b2_nullShapeId;
        }
        for (auto& brick : bricks) {
            brick.bodyId  = b2_nullBodyId;
            brick.shapeId = b2_nullShapeId;
        }
    }

    void createBounds()
    {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type      = b2_staticBody;
        b2BodyId bodyId   = b2CreateBody(worldId, &bodyDef);

        b2ShapeDef shapeDef           = b2DefaultShapeDef();
        shapeDef.material.friction    = 0.84f;
        shapeDef.material.restitution = 0.08f;

        createStaticBox(bodyId, shapeDef, 160.0f, 174.0f, 160.0f, 4.0f);
        createStaticBox(bodyId, shapeDef, -4.0f, 85.0f, 4.0f, 85.0f);
        createStaticBox(bodyId, shapeDef, 324.0f, 85.0f, 4.0f, 85.0f);
        createStaticBox(bodyId, shapeDef, 160.0f, -4.0f, 160.0f, 4.0f);
    }

    void createStaticBox(b2BodyId bodyId, const b2ShapeDef& shapeDef, float cx, float cy, float halfW, float halfH)
    {
        b2Polygon box = b2MakeOffsetBox(toMeters(halfW), toMeters(halfH), toWorldPoint(cx, cy), b2Rot_identity);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
    }

    void setupTargets(const MagicLayout& layout, int32_t xJitter, int32_t yJitter)
    {
        for (size_t i = 0; i < targets.size(); ++i) {
            Body& target = targets[i];
            if (i >= layout.targetCount) {
                target.alive      = false;
                target.puffActive = false;
                target.image->setHidden(true);
                hidePuffDots(target);
                continue;
            }

            const Point& position = layout.targets[i];
            target.alive          = true;
            target.puffActive     = false;
            target.puffSeed       = static_cast<uint32_t>(serial + i * 17);
            hidePuffDots(target);
            createDynamicCircle(target, position.x + xJitter, position.y + yJitter, kTargetSize, 0.42f, 0.75f, 0.16f);
            target.image->setHidden(false);
            lv_obj_move_foreground(target.image->raw_ptr());
        }
    }

    void setupBricks(const MagicLayout& layout, int32_t xJitter, int32_t yJitter)
    {
        for (size_t i = 0; i < bricks.size(); ++i) {
            Body& brick = bricks[i];
            if (i >= layout.brickCount) {
                brick.alive      = false;
                brick.puffActive = false;
                brick.image->setHidden(true);
                hidePuffDots(brick);
                continue;
            }

            const auto& placement = layout.bricks[i];
            brick.alive           = true;
            brick.puffActive      = false;
            createDynamicBox(brick, placement.x + xJitter, placement.y + yJitter, kBrickWidth, kBrickHeight,
                             placement.vertical ? kPi / 2.0f : 0.0f, 0.18f, 0.9f, 0.06f);
            brick.image->setHidden(false);
            lv_obj_move_foreground(brick.image->raw_ptr());
        }
    }

    void createDynamicCircle(Body& body, float x, float y, float size, float density, float friction, float restitution)
    {
        b2BodyDef bodyDef      = b2DefaultBodyDef();
        bodyDef.type           = b2_dynamicBody;
        bodyDef.position       = toWorldPoint(x + size / 2.0f, y + size / 2.0f);
        bodyDef.linearDamping  = 0.02f;
        bodyDef.angularDamping = 0.05f;
        bodyDef.enableSleep    = true;
        bodyDef.isAwake        = false;
        bodyDef.userData       = &body;
        body.bodyId            = b2CreateBody(worldId, &bodyDef);
        b2Body_SetUserData(body.bodyId, &body);

        b2ShapeDef shapeDef           = b2DefaultShapeDef();
        shapeDef.density              = density;
        shapeDef.material.friction    = friction;
        shapeDef.material.restitution = restitution;
        shapeDef.enableHitEvents      = true;
        shapeDef.userData             = &body;
        b2Circle circle               = {{0.0f, 0.0f}, toMeters(size / 2.0f)};
        body.shapeId                  = b2CreateCircleShape(body.bodyId, &shapeDef, &circle);
        b2Shape_SetUserData(body.shapeId, &body);
    }

    void createDynamicBox(Body& body, float x, float y, float width, float height, float angle, float density,
                          float friction, float restitution)
    {
        b2BodyDef bodyDef      = b2DefaultBodyDef();
        bodyDef.type           = b2_dynamicBody;
        bodyDef.position       = toWorldPoint(x + width / 2.0f, y + height / 2.0f);
        bodyDef.rotation       = b2MakeRot(angle);
        bodyDef.linearDamping  = 0.015f;
        bodyDef.angularDamping = 0.08f;
        bodyDef.enableSleep    = true;
        bodyDef.isAwake        = false;
        bodyDef.userData       = &body;
        body.bodyId            = b2CreateBody(worldId, &bodyDef);
        b2Body_SetUserData(body.bodyId, &body);

        b2ShapeDef shapeDef           = b2DefaultShapeDef();
        shapeDef.density              = density;
        shapeDef.material.friction    = friction;
        shapeDef.material.restitution = restitution;
        shapeDef.enableHitEvents      = true;
        shapeDef.userData             = &body;
        b2Polygon box                 = b2MakeBox(toMeters(width / 2.0f), toMeters(height / 2.0f));
        body.shapeId                  = b2CreatePolygonShape(body.bodyId, &shapeDef, &box);
        b2Shape_SetUserData(body.shapeId, &body);
    }

    void launchMagic()
    {
        if (launched || !b2World_IsValid(worldId)) {
            return;
        }

        launched = true;
        bowAngle = targetBowAngle;
        showStrings(false);

        b2BodyDef bodyDef      = b2DefaultBodyDef();
        bodyDef.type           = b2_dynamicBody;
        bodyDef.position       = toWorldPoint(magicPos.x + kMagicRadius, magicPos.y + kMagicRadius);
        bodyDef.linearVelocity = {toMeters(plannedMagicVel.x), toMeters(plannedMagicVel.y)};
        bodyDef.linearDamping  = 0.01f;
        bodyDef.angularDamping = 0.03f;
        bodyDef.isBullet       = true;
        magicBodyId            = b2CreateBody(worldId, &bodyDef);

        b2ShapeDef shapeDef           = b2DefaultShapeDef();
        shapeDef.density              = 3.0f;
        shapeDef.material.friction    = 0.55f;
        shapeDef.material.restitution = 0.18f;
        shapeDef.enableHitEvents      = true;
        b2Circle circle               = {{0.0f, 0.0f}, toMeters(kMagicRadius)};
        magicShapeId                  = b2CreateCircleShape(magicBodyId, &shapeDef, &circle);
    }

    void tick(uint32_t nowMs)
    {
        if (!active) {
            return;
        }

        const uint32_t elapsed = nowMs >= startMs ? nowMs - startMs : 0;
        const float dt         = std::min(kMaxCatchup, static_cast<float>(nowMs - lastTickMs) / 1000.0f);
        lastTickMs             = nowMs;
        applyOverlayOpacity(elapsed);
        updateBowAngle(elapsed);

        if (!launched && elapsed >= kLaunchMs) {
            launchMagic();
        }

        stepWorld(dt, nowMs);

        if (!launched) {
            const float pullT  = pullProgress(elapsed);
            const float easeT  = pullT * pullT * (3.0f - 2.0f * pullT);
            const float wobble = pullT > 0.0f ? std::sin(static_cast<float>(elapsed - kIdleMs) * 0.012f) * 0.7f : 0.0f;
            magicPos           = {lerp(restPos.x, aimPos.x, easeT) - launchDir.x * wobble,
                                  lerp(restPos.y, aimPos.y, easeT) - launchDir.y * wobble};
            showStrings(pullT > 0.02f);
            if (pullT > 0.02f) {
                updateStrings();
            }
        } else if (b2Body_IsValid(magicBodyId)) {
            const Point center = toScreenPoint(b2Body_GetPosition(magicBodyId));
            magicPos           = {center.x - kMagicRadius, center.y - kMagicRadius};
        }

        updatePuffs(nowMs);
        applyState();

        if (elapsed >= kLifeMs) {
            hide();
        }
    }

    void stepWorld(float dt, uint32_t nowMs)
    {
        if (!b2World_IsValid(worldId)) {
            return;
        }

        physicsRemainder += dt;
        int steps = 0;
        while (physicsRemainder >= kPhysicsStep && steps < 4) {
            b2World_Step(worldId, kPhysicsStep, 6);
            handleHitEvents(nowMs);
            physicsRemainder -= kPhysicsStep;
            ++steps;
        }
        if (steps == 4) {
            physicsRemainder = 0.0f;
        }
    }

    void handleHitEvents(uint32_t nowMs)
    {
        b2ContactEvents events = b2World_GetContactEvents(worldId);
        for (int i = 0; i < events.hitCount; ++i) {
            const b2ContactHitEvent& event = events.hitEvents[i];
            if (isMagicShape(event.shapeIdA) || isMagicShape(event.shapeIdB)) {
                wakeStructure();
            }
            if (event.approachSpeed < kHitPopSpeed) {
                continue;
            }
            handleHitShape(event.shapeIdA, event, nowMs);
            handleHitShape(event.shapeIdB, event, nowMs);
        }
    }

    bool isMagicShape(b2ShapeId shapeId) const
    {
        return b2Shape_IsValid(magicShapeId) && B2_ID_EQUALS(shapeId, magicShapeId);
    }

    void wakeStructure()
    {
        if (structureAwake) {
            return;
        }

        structureAwake = true;
        wakeBodyGroup(targets);
        wakeBodyGroup(bricks);
    }

    template <size_t N>
    void wakeBodyGroup(std::array<Body, N>& bodies)
    {
        for (auto& body : bodies) {
            if (body.alive && b2Body_IsValid(body.bodyId)) {
                b2Body_SetAwake(body.bodyId, true);
            }
        }
    }

    void handleHitShape(b2ShapeId shapeId, const b2ContactHitEvent& event, uint32_t nowMs)
    {
        if (!b2Shape_IsValid(shapeId)) {
            return;
        }

        auto* body = static_cast<Body*>(b2Shape_GetUserData(shapeId));
        if (!body || body->kind != BodyKind::Target || !body->alive) {
            return;
        }

        popBody(*body, toScreenPoint(event.point), nowMs);
    }

    float pullProgress(uint32_t elapsed) const
    {
        if (elapsed <= kIdleMs) {
            return 0.0f;
        }
        return std::clamp(static_cast<float>(elapsed - kIdleMs) / static_cast<float>(kPullMs), 0.0f, 1.0f);
    }

    float angleProgress(uint32_t elapsed) const
    {
        if (elapsed <= kIdleMs) {
            return 0.0f;
        }
        return std::clamp(static_cast<float>(elapsed - kIdleMs) / static_cast<float>(kAngleMs), 0.0f, 1.0f);
    }

    void applyOverlayOpacity(uint32_t elapsed)
    {
        const uint32_t fadeOutStart = kLifeMs > kMaskFadeMs ? kLifeMs - kMaskFadeMs : kLifeMs;
        float fade                  = 1.0f;
        if (elapsed < kMaskFadeMs) {
            fade = static_cast<float>(elapsed) / static_cast<float>(kMaskFadeMs);
        } else if (elapsed > fadeOutStart) {
            fade = 1.0f - static_cast<float>(elapsed - fadeOutStart) / static_cast<float>(kMaskFadeMs);
        }
        fade                      = std::clamp(fade, 0.0f, 1.0f);
        const lv_opa_t contentOpa = opaFromProgress(fade);
        mask->setBgOpa(static_cast<lv_opa_t>(std::round(static_cast<float>(kMaskOpacity) * fade)));
        bow->setOpa(contentOpa);
        magic->setOpa(contentOpa);
        topString.setOpa(contentOpa);
        bottomString.setOpa(contentOpa);
        applyBodyGroupOpa(bricks, contentOpa);
        applyBodyGroupOpa(targets, contentOpa);
    }

    template <size_t N>
    void applyBodyGroupOpa(std::array<Body, N>& bodies, lv_opa_t opa)
    {
        for (auto& body : bodies) {
            if (body.image) {
                body.image->setOpa(opa);
            }
            for (auto* dot : body.puffDots) {
                if (dot) {
                    lv_obj_set_style_opa(dot, opa, LV_PART_MAIN);
                }
            }
        }
    }

    void updateBowAngle(uint32_t elapsed)
    {
        if (launched) {
            bowAngle = targetBowAngle;
            return;
        }

        const float angleT = angleProgress(elapsed);
        const float easeT  = angleT * angleT * (3.0f - 2.0f * angleT);
        const float wobble =
            angleT > 0.0f && angleT < 1.0f
                ? std::sin(angleT * kPi * (3.0f + static_cast<float>((serial % 3)))) * (1.0f - angleT) * 0.16f
                : 0.0f;
        bowAngle = targetBowAngle * easeT + wobble;
    }

    Point magicCenter() const
    {
        return {magicPos.x + kMagicRadius, magicPos.y + kMagicRadius};
    }

    Point bowTopStringPoint() const
    {
        const Point local   = {-kBowWidth / 2.0f, -kBowHeight / 2.0f};
        const Point rotated = rotatePoint(local, bowAngle);
        return {bowCenter.x + rotated.x, bowCenter.y + rotated.y};
    }

    Point bowBottomStringPoint() const
    {
        const Point local   = {-kBowWidth / 2.0f, kBowHeight / 2.0f - 1.0f};
        const Point rotated = rotatePoint(local, bowAngle);
        return {bowCenter.x + rotated.x, bowCenter.y + rotated.y};
    }

    void showStrings(bool shown)
    {
        topString.setHidden(!shown);
        bottomString.setHidden(!shown);
        if (shown) {
            if (bow) {
                lv_obj_move_foreground(bow->raw_ptr());
            }
            topString.moveForeground();
            bottomString.moveForeground();
            if (magic) {
                lv_obj_move_foreground(magic->raw_ptr());
            }
        }
    }

    void moveAllForeground()
    {
        lv_obj_move_foreground(mask->raw_ptr());
        moveBodyGroupForeground(bricks);
        moveBodyGroupForeground(targets);
        lv_obj_move_foreground(bow->raw_ptr());
        topString.moveForeground();
        bottomString.moveForeground();
        lv_obj_move_foreground(magic->raw_ptr());
    }

    template <size_t N>
    void moveBodyGroupForeground(std::array<Body, N>& bodies)
    {
        for (auto& body : bodies) {
            if (body.image) {
                lv_obj_move_foreground(body.image->raw_ptr());
            }
            for (auto* dot : body.puffDots) {
                if (dot) {
                    lv_obj_move_foreground(dot);
                }
            }
        }
    }

    void updateStrings()
    {
        const Point center = magicCenter();
        topString.setPoints(bowTopStringPoint(), center);
        bottomString.setPoints(bowBottomStringPoint(), center);
        topString.moveForeground();
        bottomString.moveForeground();
    }

    void popBody(Body& body, const Point& center, uint32_t nowMs)
    {
        if (!body.alive || body.kind != BodyKind::Target) {
            return;
        }

        body.alive       = false;
        body.puffActive  = true;
        body.puffStartMs = nowMs;
        body.puffCenter  = center;
        body.image->setHidden(true);
        if (b2Body_IsValid(body.bodyId)) {
            b2DestroyBody(body.bodyId);
        }
        body.bodyId  = b2_nullBodyId;
        body.shapeId = b2_nullShapeId;
        for (auto* dot : body.puffDots) {
            if (dot) {
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(dot);
            }
        }
    }

    void hidePuffDots(Body& body)
    {
        for (auto* dot : body.puffDots) {
            if (dot) {
                lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    void updatePuffs(uint32_t nowMs)
    {
        for (auto& target : targets) {
            if (!target.puffActive) {
                continue;
            }

            const uint32_t elapsed = nowMs >= target.puffStartMs ? nowMs - target.puffStartMs : 0;
            if (elapsed >= kPuffLifeMs) {
                target.puffActive = false;
                hidePuffDots(target);
                continue;
            }

            const float t     = static_cast<float>(elapsed) / static_cast<float>(kPuffLifeMs);
            const float easeT = 1.0f - (1.0f - t) * (1.0f - t);
            const auto opa    = static_cast<lv_opa_t>(std::round(static_cast<float>(LV_OPA_COVER) * (1.0f - t)));
            for (size_t i = 0; i < target.puffDots.size(); ++i) {
                auto* dot = target.puffDots[i];
                if (!dot) {
                    continue;
                }
                const float angle = static_cast<float>(i) * 2.0f * kPi / static_cast<float>(target.puffDots.size()) +
                                    static_cast<float>(target.puffSeed % 11) * 0.13f;
                const float dist   = 3.0f + easeT * (9.0f + static_cast<float>(i % 2) * 4.0f);
                const int32_t size = static_cast<int32_t>(std::round(2.0f + (1.0f - t) * 3.0f));
                lv_obj_set_size(dot, size, size);
                lv_obj_set_pos(
                    dot, static_cast<int32_t>(std::round(target.puffCenter.x + std::cos(angle) * dist - size / 2.0f)),
                    static_cast<int32_t>(std::round(target.puffCenter.y + std::sin(angle) * dist - size / 2.0f)));
                lv_obj_set_style_bg_opa(dot, opa, LV_PART_MAIN);
            }
        }
    }

    void applyState()
    {
        if (active) {
            moveAllForeground();
        }
        if (magicVisible) {
            magic->setPos(static_cast<int32_t>(std::round(magicPos.x)), static_cast<int32_t>(std::round(magicPos.y)));
        }
        bow->setRotation(static_cast<int32_t>(std::round(bowAngle * 1800.0f / kPi)));

        applyBodyGroupState(bricks);
        applyBodyGroupState(targets);
        if (!launched) {
            updateStrings();
        }
    }

    template <size_t N>
    void applyBodyGroupState(std::array<Body, N>& bodies)
    {
        for (auto& body : bodies) {
            if (!body.alive || !b2Body_IsValid(body.bodyId)) {
                continue;
            }
            const Point center = toScreenPoint(b2Body_GetPosition(body.bodyId));
            const float angle  = b2Rot_GetAngle(b2Body_GetRotation(body.bodyId));
            body.image->setPos(static_cast<int32_t>(std::round(center.x - body.imagePivotX)),
                               static_cast<int32_t>(std::round(center.y - body.imagePivotY)));
            body.image->setRotation(static_cast<int32_t>(std::round(angle * 1800.0f / kPi)));
        }
    }

    void hide()
    {
        active         = false;
        launched       = false;
        structureAwake = false;
        magicVisible   = false;
        mask->addFlag(LV_OBJ_FLAG_HIDDEN);
        bow->setHidden(true);
        magic->setHidden(true);
        showStrings(false);
        hideBodyGroup(bricks);
        hideBodyGroup(targets);
        destroyWorld();
    }

    template <size_t N>
    void hideBodyGroup(std::array<Body, N>& bodies)
    {
        for (auto& body : bodies) {
            body.alive      = false;
            body.puffActive = false;
            body.image->setHidden(true);
            hidePuffDots(body);
            body.bodyId  = b2_nullBodyId;
            body.shapeId = b2_nullShapeId;
        }
    }
};

MagicView::MagicView(lv_obj_t* parent) : _impl(std::make_unique<Impl>(parent))
{
}

MagicView::~MagicView() = default;

void MagicView::generate(uint32_t magicSerial)
{
    if (_impl) {
        _impl->generate(magicSerial);
    }
}

void MagicView::tick(uint32_t nowMs)
{
    if (_impl) {
        _impl->tick(nowMs);
    }
}

}  // namespace files
