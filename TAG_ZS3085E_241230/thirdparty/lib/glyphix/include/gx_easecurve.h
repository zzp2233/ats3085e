/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_animation.h"

namespace gx {

namespace easing {
class CubicBezier {
public:
    CubicBezier() : m_x1(0), m_y1(0), m_x2(1), m_y2(1) {}
    CubicBezier(float x1, float y1, float x2, float y2) : m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2) {}
    float operator()(float x) const;

private:
    float m_x1, m_y1, m_x2, m_y2;
};

struct Linear {
    float operator()(float x) { return x; }
};

struct Ease {
    float operator()(float x);
};

struct EaseIn {
    float operator()(float x);
};

struct EaseOut {
    float operator()(float x);
};

struct EaseInOut {
    float operator()(float x);
};

class Spring {
public:
    explicit Spring(float spring = 1, float damping = 1, float mass = 1);
    float operator()(float x) const;

private:
    float m_tau, m_omega;
};
} // namespace easing

class AbstractEaseCurve {
public:
    AbstractEaseCurve() GX_DEFAULT;
    virtual ~AbstractEaseCurve() GX_DEFAULT;

    virtual float evaluate(float x) = 0;
};

template<class T> struct EaseCurve : AbstractEaseCurve {
    virtual float evaluate(float x) { return T()(x); }
};

class CubicBezierEaseCurve : public AbstractEaseCurve {
public:
    CubicBezierEaseCurve(float x1, float y1, float x2, float y2)
        : m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2) {}
    virtual float evaluate(float x);

private:
    float m_x1, m_y1, m_x2, m_y2;
};

class SpringEaseCurve : public AbstractEaseCurve {
public:
    explicit SpringEaseCurve(float spring = 0, float damping = 0, float mass = 1)
        : m_spring(spring, damping, mass) {}
    virtual float evaluate(float x) { return m_spring(x); }

private:
    easing::Spring m_spring;
};

class EaseCurveFactory {
public:
    enum EaseFunction { Linear, Ease, EaseIn, EaseOut, EaseInOut, CubicBezier, Spring };
    explicit EaseCurveFactory(EaseFunction function = Ease, float a = 0, float b = 0, float c = 0,
                              float d = 0);
    void set(EaseFunction function = Ease, float a = 0, float b = 0, float c = 0, float d = 0);
    GX_NODISCARD AbstractEaseCurve *easeCurve() const;

private:
    int16_t m_a, m_b, m_c, m_d; // Q6.10
};
} // namespace gx
