#include "orcsdr_orientation_policy.h"

#define GRAVITY_MIN_G2 (0.72f * 0.72f)
#define GRAVITY_MAX_G2 (1.28f * 1.28f)
#define PLANE_MIN_G 0.55f
#define SHAKE_DELTA_G2 (0.38f * 0.38f)
#define FILTER_ALPHA 0.22f

static bool finite_float(float v) { return v == v && v <= 3.4e38f && v >= -3.4e38f; }

static float boundary_ratio(float degrees)
{
    /* tan(45-h) = (1-tan(h))/(1+tan(h)); the short series is
       accurate enough for the deliberately broad UI hysteresis margin. */
    const float r = degrees * 0.01745329252f;
    const float r2 = r * r;
    const float t = r * (1.0f + r2 * (1.0f/3.0f + r2 * (2.0f/15.0f + r2 * (17.0f/315.0f))));
    return (1.0f - t) / (1.0f + t);
}

static bool valid_angle(int32_t a) { return a == 0 || a == 90 || a == 180 || a == 270; }

bool orcsdr_orientation_policy_init(orcsdr_orientation_policy_t *s,
    const orcsdr_orientation_config_t *axes, int32_t initial)
{
    if (!s || !axes || !valid_angle(initial)) return false;
    float xn = 0.0f, yn = 0.0f, xy = 0.0f;
    for (int i = 0; i != 3; ++i) {
        if (!finite_float(axes->display_x_from_sensor[i]) || !finite_float(axes->display_y_from_sensor[i])) return false;
        xn += axes->display_x_from_sensor[i] * axes->display_x_from_sensor[i];
        yn += axes->display_y_from_sensor[i] * axes->display_y_from_sensor[i];
        xy += axes->display_x_from_sensor[i] * axes->display_y_from_sensor[i];
    }
    if (xn < 0.5f || xn > 1.5f || yn < 0.5f || yn > 1.5f || xy < -0.25f || xy > 0.25f) return false;
    *s = (orcsdr_orientation_policy_t){0};
    s->config = *axes;
    if (!s->config.dwell_ms) s->config.dwell_ms = 500;
    if (!finite_float(s->config.hysteresis_deg) || s->config.hysteresis_deg < 0.0f || s->config.hysteresis_deg > 30.0f)
        s->config.hysteresis_deg = 12.0f;
    s->current_degrees = initial;
    s->stable_degrees = initial;
    return true;
}

void orcsdr_orientation_policy_enable(orcsdr_orientation_policy_t *s, bool enabled)
{
    if (!s) return;
    s->enabled = enabled;
    s->has_filter = false;
    s->stable_since_ms = 0;
}

void orcsdr_orientation_policy_set_gates(orcsdr_orientation_policy_t *s, bool touch, bool locked)
{
    if (!s) return;
    s->touch_active = touch;
    s->geometry_locked = locked;
}

bool orcsdr_orientation_policy_request_manual(orcsdr_orientation_policy_t *s, int32_t degrees)
{
    if (!s || !valid_angle(degrees)) return false;
    s->pending_degrees = degrees;
    s->pending_manual = true;
    return true;
}

static bool emit_if_open(orcsdr_orientation_policy_t *s, int32_t angle, int32_t *out)
{
    if (s->touch_active || s->geometry_locked || angle == s->current_degrees) return false;
    s->current_degrees = angle;
    *out = angle;
    return true;
}

bool orcsdr_orientation_policy_update(orcsdr_orientation_policy_t *s,
    float x, float y, float z, uint32_t now, int32_t *out)
{
    if (!s || !out) return false;
    if (s->pending_manual) {
        if (emit_if_open(s, s->pending_degrees, out)) { s->pending_manual = false; return true; }
        if (s->pending_degrees == s->current_degrees) s->pending_manual = false;
    }
    if (!finite_float(x) || !finite_float(y) || !finite_float(z)) return false;
    if (!s->enabled) return false;

    const float magnitude2 = x*x + y*y + z*z;
    if (magnitude2 < GRAVITY_MIN_G2 || magnitude2 > GRAVITY_MAX_G2) { s->stable_since_ms = 0; return false; }
    if (!s->has_filter) {
        s->filtered[0] = x; s->filtered[1] = y; s->filtered[2] = z;
        s->has_filter = true;
        s->last_sample_ms = now;
    } else {
        float delta2 = 0.0f;
        const float v[3] = {x, y, z};
        for (int i = 0; i != 3; ++i) {
            const float d = v[i] - s->filtered[i]; delta2 += d*d;
            s->filtered[i] += FILTER_ALPHA * d;
        }
        if (delta2 > SHAKE_DELTA_G2) { s->stable_since_ms = 0; s->last_sample_ms = now; return false; }
        /* A long scheduling gap cannot count toward stable dwell. */
        if ((uint32_t)(now - s->last_sample_ms) > 250u) s->stable_since_ms = 0;
        s->last_sample_ms = now;
    }

    const orcsdr_orientation_config_t *c = &s->config;
    const float px = s->filtered[0]*c->display_x_from_sensor[0] + s->filtered[1]*c->display_x_from_sensor[1] + s->filtered[2]*c->display_x_from_sensor[2];
    const float py = s->filtered[0]*c->display_y_from_sensor[0] + s->filtered[1]*c->display_y_from_sensor[1] + s->filtered[2]*c->display_y_from_sensor[2];
    /* Face-up/down is ambiguous: gravity has too little projection into the display plane. */
    const float plane2 = px*px + py*py;
    if (plane2 < PLANE_MIN_G * PLANE_MIN_G) { s->stable_since_ms = 0; return false; }
    const float ax = px < 0.0f ? -px : px;
    const float ay = py < 0.0f ? -py : py;
    /* Sector boundaries are the diagonals. Reject an angular band around each
       diagonal; tan(45-hysteresis) and its reciprocal give the band edges. */
    const float lower_ratio = boundary_ratio(c->hysteresis_deg);
    const float upper_ratio = 1.0f / lower_ratio;
    if (ax > 0.0001f && ay > 0.0001f) {
        const float ratio = ay / ax;
        if (ratio > lower_ratio && ratio < upper_ratio) { s->stable_since_ms = 0; return false; }
    }
    int32_t candidate;
    if (ax >= ay) candidate = px >= 0.0f ? 0 : 180;
    else candidate = py < 0.0f ? 90 : 270;
    if (candidate == s->current_degrees) { s->stable_degrees = candidate; s->stable_since_ms = 0; return false; }
    if (candidate != s->stable_degrees || s->stable_since_ms == 0) {
        s->stable_degrees = candidate; s->stable_since_ms = now;
        return false;
    }
    if ((uint32_t)(now - s->stable_since_ms) < c->dwell_ms) return false;
    return emit_if_open(s, candidate, out);
}
