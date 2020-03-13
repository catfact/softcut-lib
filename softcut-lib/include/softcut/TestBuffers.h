//
// Created by ezra on 11/16/18.
//

#ifndef Softcut_TESTBUFFERS_H
#define Softcut_TESTBUFFERS_H

#include <iostream>
#include <fstream>
#include <vector>

#include "Softcut.h"
#include "ReadWriteHead.h"
#include "SubHead.h"
#include "Voice.h"

namespace softcut {
    class TestBuffers {

    public:
        typedef enum {
            Rate,
            State0, State1,
            Action0, Action1,
            Phase0, Phase1,
            Fade0, Fade1,
            Rec0, Rec1,
            Pre0, Pre1,
            WrIdx0, WrIdx1,
            NumBuffers
        } BufferId;

    private:
        std::array<std::vector<float>, NumBuffers> buffers;

        template<typename T>
        void appendToBuffer(BufferId id, const T *data, size_t numFrames) {
            for (size_t i = 0; i < numFrames; ++i) {
                buffers[id].push_back(static_cast<float>(data[i]));
            }
        }

    public:
        template <int N>
        void update(Softcut<N> &cut, int voiceId, size_t numFrames) {
            update(cut.scv[voiceId].sch, numFrames);
        }
        void update(const ReadWriteHead &rwh, size_t numFrames) {
            appendToBuffer(Rate, rwh.rate.data(), numFrames);
            appendToBuffer(State0, rwh.head[0].opState.data(), numFrames);
            appendToBuffer(State1, rwh.head[1].opState.data(), numFrames);
            appendToBuffer(Action0, rwh.head[0].opAction.data(), numFrames);
            appendToBuffer(Action1, rwh.head[1].opAction.data(), numFrames);
            appendToBuffer(Phase0, rwh.head[0].phase.data(), numFrames);
            appendToBuffer(Phase1, rwh.head[1].phase.data(), numFrames);
            appendToBuffer(Fade0, rwh.head[0].fade.data(), numFrames);
            appendToBuffer(Fade1, rwh.head[1].fade.data(), numFrames);
            appendToBuffer(Rec0, rwh.head[0].rec.data(), numFrames);
            appendToBuffer(Rec1, rwh.head[1].rec.data(), numFrames);
            appendToBuffer(Pre0, rwh.head[0].pre.data(), numFrames);
            appendToBuffer(Pre1, rwh.head[1].pre.data(), numFrames);
            appendToBuffer(WrIdx0, rwh.head[0].wrIdx.data(), numFrames);
            appendToBuffer(WrIdx1, rwh.head[1].wrIdx.data(), numFrames);
        }
        const float *getBuffer(BufferId id) { return buffers[id].data(); }
    };
}
#endif