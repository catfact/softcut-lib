//
// Created by ezra on 12/6/17.
//
#include <algorithm>
#include <dsp-kit/abs.hpp>

#include "dsp-kit/clamp.hpp"

#include "softcut/Fades.h"
#include "softcut/Resampler.h"
#include "softcut/ReadWriteHead.h"
#include "softcut/DebugLog.h"

using namespace softcut;
using namespace std;
using namespace dspkit;

ReadWriteHead::ReadWriteHead() {
    lastFrameIdx = 0;
    readDuckRamp.setSampleRate(48000.f);
    readDuckRamp.setTime(0.01);
    writeDuckRamp.setSampleRate(48000.f);
    writeDuckRamp.setTime(0.01);
    this->init();
}

void ReadWriteHead::init() {
    start = 0.f;
    end = maxBlockSize * 2;
    head[0].init(this);
    head[1].init(this);
    rate.fill(1.f);
    pre.fill(0.f);
    rec.fill(0.f);
    active.fill(-1);
    lastFrameIdx = 0;
    fadeInc = 1.f;
}

void ReadWriteHead::handlePhaseResult(frame_t fr, const SubHead::PhaseResult *res) {
    bool breakout = false;
    for (int h = 0; h < 2; ++h) {
        if (breakout) { break; }
        int k;
        switch (res[h]) {
            case SubHead::PhaseResult::Stopped:
                head[h].playState[fr] = SubHead::PlayState::Stopped;
                break;
            case SubHead::PhaseResult::Playing:
                head[h].playState[fr] = SubHead::PlayState::Playing;
                break;
            case SubHead::PhaseResult::FadeIn:
                head[h].playState[fr] = SubHead::PlayState::FadeIn;
                break;
            case SubHead::PhaseResult::FadeOut:
                head[h].playState[fr] = SubHead::PlayState::FadeOut;
                break;
            case SubHead::PhaseResult::DoneFadeIn:
                head[h].playState[fr] = SubHead::PlayState::Playing;
                break;
            case SubHead::PhaseResult::DoneFadeOut:
                head[h].playState[fr] = SubHead::PlayState::Stopped;
                break;
            case SubHead::PhaseResult::CrossLoopEnd:
                head[h].playState[fr] = SubHead::PlayState::FadeOut;
                k = h > 0 ? 0 : 1;
                assert((res[k] != SubHead::PhaseResult::CrossLoopEnd)
                       && (res[k] != SubHead::PhaseResult::CrossLoopStart));
                head[k].setPosition(fr, start); // TODO: process ping-pong direction
                head[k].playState[fr] = SubHead::PlayState::FadeIn;
                breakout = true;
                break;
            case SubHead::PhaseResult::CrossLoopStart:
                head[h].playState[fr] = SubHead::PlayState::FadeOut;
                k = h > 0 ? 0 : 1;
                assert((res[k] != SubHead::PhaseResult::CrossLoopEnd)
                       && (res[k] != SubHead::PhaseResult::CrossLoopStart));
                head[k].setPosition(fr, end); // TODO: process ping-pong direction
                head[k].playState[fr] = SubHead::PlayState::FadeIn;
                breakout = true;
                break;
            default:
                assert(false); // nope
        }
    }
}

void ReadWriteHead::updateSubheadState(size_t numFrames) {
    SubHead::PhaseResult res[2];
    frame_t fr_1 = lastFrameIdx;
    frame_t fr = 0;
    if (requestedPosition >= 0.0) {
        bool didChangePosition = false;
        while (fr < numFrames) {
            for (int h = 0; h < 2; ++h) {
                res[h] = head[h].updatePhase(fr_1, fr);
            }
            if (!didChangePosition) {
                for (int h = 0; h < 2; ++h) {
                    if (res[h] == SubHead::PhaseResult::Stopped || res[h] == SubHead::PhaseResult::DoneFadeOut) {
                        performPositionChange(h, fr, requestedPosition, res);
                        requestedPosition = -1.0;
                        didChangePosition = true;
                        break;
                    }
                }
                if (!didChangePosition) {
                    handlePhaseResult(fr, res);
                }
            } else {
                handlePhaseResult(fr, res);
            }
            fr_1 = fr++;
        }
    } else {
        // no position change request
        while (fr < numFrames) {
            for (int h = 0; h < 2; ++h) {
                res[h] = head[h].updatePhase(fr_1, fr);
            }
            handlePhaseResult(fr, res);
            fr_1 = fr++;
        }
    }
    updateActiveState(fr);
    lastFrameIdx = fr_1;
}


void ReadWriteHead::updateActiveState(frame_t fr) {
    switch (head[0].playState[fr]) {
        case SubHead::PlayState::Stopped:
        case SubHead::PlayState::FadeOut:
            active[fr] = head[1].playState[fr] == SubHead::PlayState::Stopped ? -1 : 1;
            break;
        case SubHead::PlayState::Playing:
        case SubHead::PlayState::FadeIn:
            active[fr] = 0;
            assert(head[1].playState[fr] == SubHead::PlayState::Stopped
                   || head[1].playState[fr] == SubHead::PlayState::FadeOut);
            break;
    }
}

void ReadWriteHead::performPositionChange(int h, frame_t fr, phase_t pos, const SubHead::PhaseResult *res) {
    head[h].setPosition(fr, pos);
    head[h].playState[fr] = SubHead::PlayState::FadeIn;
    int k = h > 0 ? 0 : 1;
    switch (res[k]) {
        case SubHead::PhaseResult::Playing:
        case SubHead::PhaseResult::DoneFadeIn:
        case SubHead::PhaseResult::CrossLoopStart:
        case SubHead::PhaseResult::CrossLoopEnd:
        case SubHead::PhaseResult::FadeOut:
        case SubHead::PhaseResult::FadeIn: // <-- FIXME: could cause spiky envelope
            head[k].playState[fr] = SubHead::PlayState::FadeOut;
            break;
        case SubHead::PhaseResult::Stopped:
        case SubHead::PhaseResult::DoneFadeOut:
            head[k].playState[fr] = SubHead::PlayState::Stopped;
            break;
    }
}

void ReadWriteHead::updateSubheadWriteLevels(size_t numFrames) {
    for (frame_t fr = 0; fr < numFrames; ++fr) {
        head[0].calcFrameLevels(fr);
        head[1].calcFrameLevels(fr);
    }
}

void ReadWriteHead::performSubheadWrites(const float *input, size_t numFrames) {
    frame_t fr_1 = lastFrameIdx;
    frame_t fr = 0;
    while (fr < numFrames) {
        head[0].performFrameWrite(fr_1, fr, input[fr]);
        head[1].performFrameWrite(fr_1, fr, input[fr]);
        fr_1 = fr;
        ++fr;
    }
    lastFrameIdx = fr_1;
}

void ReadWriteHead::performSubheadReads(float *output, size_t numFrames) {
    float out0;
    float out1;
    unsigned int activeHeadBits;
    unsigned int active0;
    unsigned int active1;
    for (size_t fr = 0; fr < numFrames; ++fr) {
        active0 = static_cast<unsigned int>(
                (head[0].playState[fr] != SubHead::PlayState::Stopped)
                && (head[0].fade[fr] > 0.f));
        active1 = static_cast<unsigned int>(
                (head[1].playState[fr] != SubHead::PlayState::Stopped)
                && (head[1].fade[fr] > 0.f));
        activeHeadBits = active0 | (active1 << 1u);
        auto frIdx = static_cast<frame_t>(fr);
        switch (activeHeadBits) {
            case 0:
                output[fr] = 0.f;
                break;
            case 1:
                output[fr] = head[0].fade[fr] * head[0].performFrameRead(frIdx);
                break;
            case 2:
                output[fr] = head[1].fade[fr] * head[1].performFrameRead(frIdx);
                break;
            case 3:
                out0 = this->head[0].performFrameRead(frIdx);
                out1 = this->head[1].performFrameRead(frIdx);
                output[fr] = mixFade(out0, out1, head[0].fade[fr], head[1].fade[fr]);
                break;
            default:
                output[fr] = 0.f;
        }
    }
}

void ReadWriteHead::setSampleRate(float aSr) {
    this->sr = aSr;
    // TODO: refresh dependent variables..
    /// though in present applications, we are unlikely to change SR at runtime.
}

void ReadWriteHead::setBuffer(sample_t *b, uint32_t sz) {
    this->buf = b;
    head[0].setBuffer(b, sz);
    head[1].setBuffer(b, sz);
}

void ReadWriteHead::setLoopStartSeconds(float x) {
    this->start = x * sr;
}

void ReadWriteHead::setRecOffsetSamples(int d) {
    this->recOffsetSamples = d;
}

void ReadWriteHead::setLoopEndSeconds(float x) {
    this->end = x * sr;
}

void ReadWriteHead::setFadeTime(float secs) {
    if (secs > 0.f) {
        this->fadeInc = 1.f / (secs * sr);
    } else {
        this->fadeInc = 1.f;
    }
}

void ReadWriteHead::setLoopFlag(bool val) {
    this->loopFlag = val;
}

void ReadWriteHead::setRate(size_t i, rate_t x) {
    rate[i] = x;
//    rateDirMul[i] = x > 0.f ? 1 : -1;
}

void ReadWriteHead::setRec(size_t i, float x) {
    rec[i] = x;
}

void ReadWriteHead::setPre(size_t i, float x) {
    pre[i] = x;
}

void ReadWriteHead::requestPosition(float seconds) {
    requestedPosition = seconds * sr;
}

phase_t ReadWriteHead::getActivePhase() const {
    // return the last written phase for the last active head
    return head[active[lastFrameIdx]].phase[lastFrameIdx];
}

double ReadWriteHead::getRateBuffer(size_t i) {
    return rate[i];
}

void ReadWriteHead::copySubheadPositions(const ReadWriteHead &src, size_t numFrames) {
    lastFrameIdx = src.lastFrameIdx;
    fadeInc = src.fadeInc;
    loopFlag = src.loopFlag;
    recOffsetSamples = src.recOffsetSamples;
    start = src.start;
    end = src.end;
    sr = src.sr;
    std::copy_n(src.rate.begin(), numFrames, rate.begin());
//    std::copy_n(src.dir.begin(), numFrames, dir.begin());
    std::copy_n(src.active.begin(), numFrames, active.begin());
    for (int h = 0; h < 2; ++h) {
        std::copy_n(src.head[h].rateDirMul.begin(), numFrames, head[h].rateDirMul.begin());
        std::copy_n(src.head[h].phase.begin(), numFrames, head[h].phase.begin());
        std::copy_n(src.head[h].wrIdx.begin(), numFrames, head[h].wrIdx.begin());
        std::copy_n(src.head[h].fade.begin(), numFrames, head[h].fade.begin());
        std::copy_n(src.head[h].playState.begin(), numFrames, head[h].playState.begin());
//        std::copy_n(src.head[h].opAction.begin(), numFrames, head[h].opAction.begin());
    }
}

void ReadWriteHead::computeReadDuckLevels(const ReadWriteHead *other, size_t numFrames) {
    float x;
    for (size_t fr = 0; fr < numFrames; ++fr) {
        x = computeReadDuckLevel(&(head[0]), &(other->head[0]), fr);
        x = clamp_lo<float>(x, computeReadDuckLevel(&(head[0]), &(other->head[1]), fr));
        x = clamp_lo<float>(x, computeReadDuckLevel(&(head[1]), &(other->head[0]), fr));
        x = clamp_lo<float>(x, computeReadDuckLevel(&(head[1]), &(other->head[1]), fr));
        readDuck[fr] = readDuckRamp.getNextValue(1.f - x);
    }
}

void ReadWriteHead::applyReadDuckLevels(float *output, size_t numFrames) {
#if 1
    for (size_t fr = 0; fr < numFrames; ++fr) {
        output[fr] *= readDuck[fr];
    }
#endif
}

void ReadWriteHead::computeWriteDuckLevels(const ReadWriteHead *other, size_t numFrames) {
    float x;
    for (size_t fr = 0; fr < numFrames; ++fr) {
        x = computeWriteDuckLevel(&(head[0]), &(other->head[0]), fr);
        x = clamp_lo<float>(x, computeWriteDuckLevel(&(head[0]), &(other->head[1]), fr));
        x = clamp_lo<float>(x, computeWriteDuckLevel(&(head[1]), &(other->head[0]), fr));
        x = clamp_lo<float>(x, computeWriteDuckLevel(&(head[1]), &(other->head[1]), fr));
        writeDuck[fr] = x;
    }
}

void ReadWriteHead::applyWriteDuckLevels(size_t numFrames) {
    float x, y;
    for (size_t fr = 0; fr < numFrames; ++fr) {
        x = writeDuck[fr];
        y = writeDuckRamp.getNextValue(x);
        rec[fr] *= (1.f - y);
        pre[fr] += (1.f - pre[fr]) * y;
    }
}


float ReadWriteHead::computeReadDuckLevel(const SubHead *a, const SubHead *b, size_t frame) {
    /// FIXME: for this to really work, position needs to be calculated modulo loop points.
    //// as it is, we get artifacts when one or both voices cross the loop point,
    //// while being near each other on one side of it.
    //// running the duck level through a smoother is an attempt to mitigate these artifacts...
    static constexpr float recMin = std::numeric_limits<float>::epsilon() * 2.f;
    static constexpr float fadeMin = std::numeric_limits<float>::epsilon() * 2.f;
    static constexpr float preMax = 1.f - (std::numeric_limits<float>::epsilon() * 2.f);
    // FIXME: these magic numbers are unaffected by sample rate :/
    static constexpr phase_t dmax = 480 * 2;
    static constexpr phase_t dmin = 480;
    // if `a` fade level is ~0, no ducking is needed
    if (a->fade[frame] < fadeMin) { return 0.f; }
    // if `b` record level is ~0, and pre level is ~1, no ducking is needed
    if ((b->rec[frame] < recMin) && (b->pre[frame] > preMax)) { return 0.f; }
    // FIXME: this is where we probably need to compute distance modulo loop points...
    const auto dabs = dspkit::abs<phase_t>(a->phase[frame] - b->phase[frame]);
    if (dabs >= dmax) { return 0.f; }
    if (dabs <= dmin) { return 1.f; }
    return Fades::raisedCosFadeOut(static_cast<float>((dabs - dmin) / (dmax - dmin)));
}


float ReadWriteHead::computeWriteDuckLevel(const SubHead *a, const SubHead *b, size_t frame) {
    static constexpr float recMin = std::numeric_limits<float>::epsilon() * 2.f;
    static constexpr float preMax = 1.f - (std::numeric_limits<float>::epsilon() * 2.f);
    static constexpr phase_t dmax = 480 * 2;
    static constexpr phase_t dmin = 480;
    if (a->rec[frame] < recMin && a->pre[frame] > preMax) { return 0.f; }
    if (b->rec[frame] < recMin && b->pre[frame] > preMax) { return 0.f; }
    const auto dabs = dspkit::abs<phase_t>(a->phase[frame] - b->phase[frame]);
    if (dabs >= dmax) { return 0.f; }
    if (dabs <= dmin) { return 1.f; }
    return Fades::raisedCosFadeOut(static_cast<float>((dabs - dmin) / (dmax - dmin)));
}


