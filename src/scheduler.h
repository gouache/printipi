/* The MIT License (MIT)
 *
 * Copyright (c) 2014 Colin Wallace
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <cassert> //for assert
#include <array>
#include "outputevent.h"
#include "common/logging.h"
#include "common/intervaltimer.h"
#include "compileflags.h"
#include "platforms/auto/thisthreadsleep.h" //for SleepT
#include "platforms/auto/primitiveiopin.h"
#include "iodrivers/iopin.h"

#if USE_PTHREAD
    #include <pthread.h> //for pthread_setschedparam
#endif
#include "schedulerbase.h"


/* 
 * The Scheduler controls program flow between tending communications and executing events at precise times.
 * It is designed to run in a single-threaded environment so it can have maximum control.
 * Scheduler.eventLoop should be called after any program setup is completed.
 * The eventLoop function will frequently yield control *briefly* to Interface.onIdleCpu.
 * This gives the onIdleCpu function the possibility to schedule events using Scheduler.queue.
 */
template <typename Interface> class Scheduler : public SchedulerBase {
    EventClockT::duration MAX_SLEEP; //need to call onIdleCpu handlers every so often, even if no events are ready.
    Interface interface;
    OutputEvent nextEvent;
    bool _doExit;
    public:
        void queue(const OutputEvent &evt);
        template <typename T> void setMaxSleep(T duration) {
            MAX_SLEEP = std::chrono::duration_cast<EventClockT::duration>(duration);
        }
        inline void setDefaultMaxSleep() {
            setMaxSleep(std::chrono::milliseconds(40));
        }
        Scheduler(Interface interface);
        void initSchedThread() const; //call this from whatever threads call nextEvent to optimize that thread's priority.
        bool isRoomInBuffer() const;
        void eventLoop();
        void exitEventLoop();
    private:
        void sleepUntilEvent(const OutputEvent &evt) const;
        bool isEventTime(const OutputEvent &evt) const;
};

template <typename Interface> Scheduler<Interface>::Scheduler(Interface interface) 
    : interface(interface), _doExit(false) {
    setDefaultMaxSleep();
}


template <typename Interface> void Scheduler<Interface>::queue(const OutputEvent &evt) {
    this->nextEvent = evt;
}

template <typename Interface> void Scheduler<Interface>::initSchedThread() const {
    #if USE_PTHREAD
        struct sched_param sp; 
        sp.sched_priority=SCHED_PRIORITY; 
        if (int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp)) {
            LOGW("Warning: pthread_setschedparam (increase thread priority) at scheduler.cpp returned non-zero: %i\n", ret);
        } else {
            LOG("Set pthread sched_priority\n");
        }
    #endif
}

template <typename Interface> bool Scheduler<Interface>::isRoomInBuffer() const {
    return nextEvent.isNull();
}


template <typename Interface> void Scheduler<Interface>::eventLoop() {
    OnIdleCpuIntervalT intervalT = OnIdleCpuIntervalWide;
    int numShortIntervals = 0; //need to track the number of short cpu intervals, because if we just execute short intervals constantly for, say, 1 second, then certain services that only run at long intervals won't occur. So make every, say, 10000th short interval transform into a wide interval.
    while (!_doExit) {
        if (!nextEvent.isNull() && isEventTime(nextEvent)) {
            //queue the pending event and reset it
            LOGV("Scheduler::queue\n");
            interface.queue(nextEvent);
            this->nextEvent = OutputEvent();
        }
        if (!interface.onIdleCpu(intervalT)) {
            if (_doExit) {
                //check exit flag (may have changed in onIdleCpu call) again before entering a long sleep
                break;
            }
            //if we don't need any onIdleCpu, then sleep until the event.
            //sleepUntilEvent won't always do the full sleep; it has a time limit.
            LOGV("Scheduler::sleepUntilEvent\n");
            this->sleepUntilEvent(this->nextEvent);
            //We just slept for a while, which translates to a wide interval. Note that it may not actually be the event time yet.
            intervalT = OnIdleCpuIntervalWide;
            //numShortIntervals = 0;
        } else {
            //after 2048 (just a nice binary number) short intervals, insert a wide interval instead (this won't force a sleep).
            intervalT = (++numShortIntervals % 2048) ? OnIdleCpuIntervalShort : OnIdleCpuIntervalWide;
        }
    }
    LOGV("Scheduler::eventLoop is exiting\n");
    _doExit = false;
}

template <typename Interface> void Scheduler<Interface>::exitEventLoop() {
    _doExit = true;
}

template <typename Interface> void Scheduler<Interface>::sleepUntilEvent(const OutputEvent &evt) const {
    //need to call onIdleCpu handlers occasionally - avoid sleeping for long periods of time.
    auto sleepUntil = EventClockT::now() + MAX_SLEEP;
    //allow calling with a null OutputEvent to sleep for a configured period of time (MAX_SLEEP)
    if (!evt.isNull()) {
        auto evtTime = interface.schedTime(evt.time());
        if (evtTime < sleepUntil) {
            sleepUntil = evtTime;
        }
    }
    SleepT::sleep_until(sleepUntil);
}

template <typename Interface> bool Scheduler<Interface>::isEventTime(const OutputEvent &evt) const {
    return interface.schedTime(evt.time()) <= EventClockT::now();
}

#endif
