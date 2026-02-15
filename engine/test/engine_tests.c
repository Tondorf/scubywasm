#include "engine.c"

#include <math.h>
#include <stdio.h>

#include "test/unity.h"

#define DEG2RAD .017453293F

struct Config cfg;
struct Context *ctx;

void setUp(void)
{
    set_default_config(&cfg);
    ctx = create_context(&cfg);
}

void tearDown(void)
{
    free_context(ctx);
}

void test_set_default_config_sets_default_values(void)
{
    set_default_config(&cfg);

    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_SHIP_MAX_TURN_RATE, cfg.ship_max_turn_rate);
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_SHIP_MAX_VELOCITY, cfg.ship_max_velocity);
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_SHIP_HIT_RADIUS, cfg.ship_hit_radius);
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_SHOT_VELOCITY, cfg.shot_velocity);
    TEST_ASSERT_EQUAL_INT32(DEFAULT_SHOT_LIFETIME, cfg.shot_lifetime);
}

void test_add_agent_adds_ships_and_no_shots(void)
{
    const struct Pose init_pose1 = {.x = .5F, .y = 0.F, .heading = 45.F};
    const struct Pose init_pose2 = {.x = 0.F, .y = .5F, .heading = 300.F};

    const uint32_t id1 = add_agent(ctx, &init_pose1);
    const uint32_t id2 = add_agent(ctx, &init_pose2);
    TEST_ASSERT_GREATER_THAN(0, id1);
    TEST_ASSERT_GREATER_THAN(0, id2);
    TEST_ASSERT_TRUE(id1 != id2);

    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, id1));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, id2));

    struct Pose pose;

    get_ship_pose(ctx, id1, &pose);
    TEST_ASSERT_EQUAL_FLOAT(init_pose1.x, pose.x);
    TEST_ASSERT_EQUAL_FLOAT(init_pose1.y, pose.y);
    TEST_ASSERT_FLOAT_WITHIN(.1F, init_pose1.heading, pose.heading);
    TEST_ASSERT_EQUAL_INT32(0, get_shot_pose(ctx, id1, &pose));
    TEST_ASSERT_EQUAL_INT32(0, get_score(ctx, id1));

    get_ship_pose(ctx, id2, &pose);
    TEST_ASSERT_EQUAL_FLOAT(init_pose2.x, pose.x);
    TEST_ASSERT_EQUAL_FLOAT(init_pose2.y, pose.y);
    TEST_ASSERT_FLOAT_WITHIN(.1F, init_pose2.heading, pose.heading);
    TEST_ASSERT_EQUAL_INT32(0, get_shot_pose(ctx, id2, &pose));
    TEST_ASSERT_EQUAL_INT32(0, get_score(ctx, id2));
}

void test_shot_spawn_outside_of_ship_hit_radius(void)
{
    const float threshold = 1.F / SHOT_SPAWN_CLEARANCE_FACTOR;

    for (int i = 0; i < 25; i++)
    {
        const float angle = (float)i * 15.F; // 0, 15, 30, ..., 360
        const float norm = hypotf(approx_sin(angle), approx_cos(angle));

        char msg[32];
        snprintf(msg, sizeof(msg), "Angle: %.0f degrees.", (double)angle);
        TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(threshold, norm, msg);
    }
}

void test_tick_wraps_ship_over_world_edges(void)
{
    const float v = cfg.ship_max_velocity;

    // one ship per edge, positioned to cross the boundary in one thrust-tick.
    struct Pose p_right = {.x = 1.F - .5F * v, .y = .25F, .heading = 90.F};
    struct Pose p_left = {.x = .5F * v, .y = .75F, .heading = 270.F};
    struct Pose p_top = {.x = .25F, .y = 1.F - .5F * v, .heading = 0.F};
    struct Pose p_bottom = {.x = .75F, .y = .5F * v, .heading = 180.F};

    const uint32_t id_right = add_agent(ctx, &p_right);
    const uint32_t id_left = add_agent(ctx, &p_left);
    const uint32_t id_top = add_agent(ctx, &p_top);
    const uint32_t id_bottom = add_agent(ctx, &p_bottom);

    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id_right, ACTION_THRUST));
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id_left, ACTION_THRUST));
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id_top, ACTION_THRUST));
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id_bottom, ACTION_THRUST));

    // all four should remain alive; no collisions expected
    TEST_ASSERT_EQUAL_UINT32(4U, tick(ctx, 1U));

    get_ship_pose(ctx, id_right, &p_right);
    get_ship_pose(ctx, id_left, &p_left);
    get_ship_pose(ctx, id_top, &p_top);
    get_ship_pose(ctx, id_bottom, &p_bottom);

    // right edge: x wraps from ~1 to small positive
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .5F * v, p_right.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .25F, p_right.y);

    // left edge: x wraps from small negative to ~1
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 1.F - .5F * v, p_left.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .75F, p_left.y);

    // top edge: y wraps from ~1 to small positive
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .25F, p_top.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .5F * v, p_top.y);

    // bottom edge: y wraps from small negative to ~1
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, .75F, p_bottom.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 1.F - .5F * v, p_bottom.y);
}

void test_tick_single_agent_turn_then_move(void)
{
    const struct Pose init_pose = {.x = .25F, .y = .25F, .heading = 90.F};
    const uint32_t id = add_agent(ctx, &init_pose);

    // tick 1:
    // apply turn-right only (no translation)
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id, ACTION_TURN_RIGHT));

    TEST_ASSERT_EQUAL_UINT32(1U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, id));

    struct Pose p1;
    get_ship_pose(ctx, id, &p1);

    // heading increases by max turn rate; position unchanged
    const float heading = init_pose.heading + cfg.ship_max_turn_rate;
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, init_pose.x, p1.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, init_pose.y, p1.y);
    TEST_ASSERT_FLOAT_WITHIN(.1F, heading, p1.heading);

    // tick 2:
    // apply thrust only (no further rotation)
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id, ACTION_THRUST));

    TEST_ASSERT_EQUAL_UINT32(1U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, id));

    struct Pose p2;
    get_ship_pose(ctx, id, &p2);

    // integrate one step along current heading
    const float x = p1.x + cfg.ship_max_velocity * sinf(p1.heading * DEG2RAD);
    const float y = p1.y + cfg.ship_max_velocity * cosf(p1.heading * DEG2RAD);

    TEST_ASSERT_FLOAT_WITHIN(1e-5F, x, p2.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5F, y, p2.y);
    TEST_ASSERT_FLOAT_WITHIN(.1F, p1.heading, p2.heading);
}

void test_tick_kills_both_on_ship_collision(void)
{
    const float r = cfg.ship_hit_radius;

    /*
     * Start *just* outside the collision radius (2r), then thrust both ships
     * towards each other so that they collide within the next tick.
     */
    const float d = 2.F * r + .5F * cfg.ship_max_velocity;
    const struct Pose p1 = {.x = .5F, .y = .5F, .heading = 0.F};
    const struct Pose p2 = {.x = .5F, .y = .5F + d, .heading = 180.F};

    const uint32_t id1 = add_agent(ctx, &p1);
    const uint32_t id2 = add_agent(ctx, &p2);

    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id1, ACTION_THRUST));
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, id2, ACTION_THRUST));

    // one tick should resolve the ship <> ship collision and kill both
    TEST_ASSERT_EQUAL_UINT32(0U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(0, is_alive(ctx, id1));
    TEST_ASSERT_EQUAL_INT32(0, is_alive(ctx, id2));

    // ship <> ship collision penalizes both
    TEST_ASSERT_EQUAL_INT32(-1, get_score(ctx, id1));
    TEST_ASSERT_EQUAL_INT32(-1, get_score(ctx, id2));
}

void test_tick_kills_on_shot_hit(void)
{
    const float r = cfg.ship_hit_radius;
    const float shooter_x = .5F;
    const float shooter_y = .2F;

    /*
     * Place the target so that:
     * - no collision on tick 1 (endpoint is still > r away),
     * - collision on tick 2 (endpoint becomes < r away),
     * while keeping ships themselves farther apart than 2r to avoid
     * ship <> ship.
     */
    const float target_y = shooter_y
        + (r * SHOT_SPAWN_CLEARANCE_FACTOR) // shot spawn clearance from shooter
        + r                                 // ship hit radius
        + (1.5F * cfg.shot_velocity);       // makes it hit on the 2nd tick

    const struct Pose shooter_pose = {
        .x = shooter_x, .y = shooter_y, .heading = 0.F};
    const struct Pose target_pose = {
        .x = shooter_x, .y = target_y, .heading = 180.F};

    const uint32_t shooter_id = add_agent(ctx, &shooter_pose);
    const uint32_t target_id = add_agent(ctx, &target_pose);

    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, shooter_id, ACTION_FIRE));

    struct Pose shot_pose;
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime,
                            get_shot_pose(ctx, shooter_id, &shot_pose));

    // tick 1:
    // shot advances, but should not hit yet
    TEST_ASSERT_EQUAL_UINT32(2U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, target_id));
    TEST_ASSERT_EQUAL_INT32(0, get_score(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(0, get_score(ctx, target_id));
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime - 1,
                            get_shot_pose(ctx, shooter_id, &shot_pose));

    // tick 2:
    // shot should collide with target, consuming the shot and updating scores
    TEST_ASSERT_EQUAL_UINT32(1U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(0, is_alive(ctx, target_id));
    TEST_ASSERT_EQUAL_INT32(2, get_score(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(-1, get_score(ctx, target_id));
    TEST_ASSERT_EQUAL_INT32(0, get_shot_pose(ctx, shooter_id, &shot_pose));
}

void test_set_action_only_allows_refire_after_shot_vanishes(void)
{
    // same setup as in test_tick_kills_on_shot_hit()
    const float r = cfg.ship_hit_radius;
    const float shooter_x = .5F;
    const float shooter_y = .2F;
    const float target_y = shooter_y + (r * SHOT_SPAWN_CLEARANCE_FACTOR) + r
        + (1.5F * cfg.shot_velocity);

    const struct Pose shooter_pose = {
        .x = shooter_x, .y = shooter_y, .heading = 0.F};
    const struct Pose target_pose = {
        .x = shooter_x, .y = target_y, .heading = 180.F};

    const uint32_t shooter_id = add_agent(ctx, &shooter_pose);
    const uint32_t target_id = add_agent(ctx, &target_pose);

    // fire: spawn a shot
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, shooter_id, ACTION_FIRE));

    struct Pose p0;
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime,
                            get_shot_pose(ctx, shooter_id, &p0));

    // tick 1:
    // shot advances, but should not hit yet
    TEST_ASSERT_EQUAL_UINT32(2U, tick(ctx, 1U));

    struct Pose p1;
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime - 1,
                            get_shot_pose(ctx, shooter_id, &p1));
    TEST_ASSERT_TRUE(p1.y > p0.y);

    // fire again while shot is alive: must not respawn/reset
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, shooter_id, ACTION_FIRE));

    struct Pose p2;
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime - 1,
                            get_shot_pose(ctx, shooter_id, &p2));
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, p1.x, p2.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, p1.y, p2.y);

    // tick 2:
    // shot hits target, target dies, shot vanishes
    TEST_ASSERT_EQUAL_UINT32(1U, tick(ctx, 1U));
    TEST_ASSERT_EQUAL_INT32(1, is_alive(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(0, is_alive(ctx, target_id));
    TEST_ASSERT_EQUAL_INT32(2, get_score(ctx, shooter_id));
    TEST_ASSERT_EQUAL_INT32(-1, get_score(ctx, target_id));

    struct Pose p3;
    TEST_ASSERT_EQUAL_INT32(0, get_shot_pose(ctx, shooter_id, &p3));

    // after vanish: firing is allowed again and spawns a fresh shot at the
    // muzzle
    TEST_ASSERT_EQUAL_INT32(0, set_action(ctx, shooter_id, ACTION_FIRE));

    struct Pose p4;
    TEST_ASSERT_EQUAL_INT32(cfg.shot_lifetime,
                            get_shot_pose(ctx, shooter_id, &p4));
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, p0.x, p4.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, p0.y, p4.y);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_set_default_config_sets_default_values);
    RUN_TEST(test_add_agent_adds_ships_and_no_shots);
    RUN_TEST(test_shot_spawn_outside_of_ship_hit_radius);
    RUN_TEST(test_tick_wraps_ship_over_world_edges);
    RUN_TEST(test_tick_single_agent_turn_then_move);
    RUN_TEST(test_tick_kills_both_on_ship_collision);
    RUN_TEST(test_tick_kills_on_shot_hit);
    RUN_TEST(test_set_action_only_allows_refire_after_shot_vanishes);

    return UNITY_END();
}
