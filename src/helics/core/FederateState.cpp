/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#include "FederateState.h"

#include "EndpointInfo.h"
#include "FilterInfo.h"
#include "PublicationInfo.h"
#include "SubscriptionInfo.h"
#include "TimeCoordinator.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "config.h"
#include <boost/foreach.hpp>

#define LOG_ERROR(message) logMessage (0, "", message);
#define LOG_WARNING(message) logMessage (1, "", message);

#ifdef LOGGING_ENABLED
#define LOG_NORMAL(message)                                                                                       \
    if (logLevel >= 2)                                                                                            \
    {                                                                                                             \
        logMessage (2, "", message);                                                                              \
    }

#ifdef DEBUG_LOGGING_ENABLED
#define LOG_DEBUG(message)                                                                                        \
    if (logLevel >= 3)                                                                                            \
    {                                                                                                             \
        logMessage (3, "", message);                                                                              \
    }
#else
#define LOG_DEBUG(message)
#endif

#ifdef TRACE_LOGGING_ENABLED
#define LOG_TRACE(message)                                                                                        \
    if (logLevel >= 4)                                                                                            \
    {                                                                                                             \
        logMessage (4, "", message);                                                                              \
    }
#else
#define LOG_TRACE(message)
#endif
#else
#define LOG_NORMAL(message)
#define LOG_DEBUG(message)
#define LOG_TRACE(message)
#endif

namespace helics
{
FederateState::FederateState (const std::string &name_, const CoreFederateInfo &info_) : name (name_)
{
    state = HELICS_CREATED;
    timeCoord = std::make_unique<TimeCoordinator> (info_);
    logLevel = info_.logLevel;
}

FederateState::~FederateState () = default;

// define the allowable state transitions for a federate
void FederateState::setState (helics_federate_state_type newState)
{
    if (state == newState)
    {
        return;
    }
    switch (newState)
    {
    case HELICS_ERROR:
    case HELICS_FINISHED:
    case HELICS_CREATED:
        state = newState;
        break;
    case HELICS_INITIALIZING:
    {
        auto reqState = HELICS_CREATED;
        state.compare_exchange_strong (reqState, newState);
        break;
    }
    case HELICS_EXECUTING:
    {
        auto reqState = HELICS_INITIALIZING;
        state.compare_exchange_strong (reqState, newState);
        break;
    }
    case HELICS_NONE:
    default:
        break;
    }
}

void FederateState::reset ()
{
    state = HELICS_CREATED;
    // TODO:: this probably needs to do a lot more
}
/** reset the federate to the initializing state*/
void FederateState::reInit ()
{
    state = HELICS_INITIALIZING;
    // TODO:: this needs to reset a bunch of stuff as well as check a few things
}
helics_federate_state_type FederateState::getState () const { return state; }

int32_t FederateState::getCurrentIteration () const { return timeCoord->getCurrentIteration (); }

void FederateState::setParent (CommonCore *coreObject)
{
    parent_ = coreObject;
    timeCoord->setMessageSender ([coreObject](const ActionMessage &msg) { coreObject->addActionMessage (msg); });
}

static auto compareFunc = [](const auto &A, const auto &B) { return (A->id < B->id); };

CoreFederateInfo FederateState::getInfo () const
{
    // lock the mutex to ensure we have the latest values
    std::lock_guard<std::mutex> lock (_mutex);
    return timeCoord->getFedInfo ();
}

void FederateState::UpdateFederateInfo (CoreFederateInfo &newInfo)
{
    // TODO:: change the check into the timeCoord
    if (newInfo.timeDelta <= timeZero)
    {
        newInfo.timeDelta = timeEpsilon;
    }
    std::lock_guard<std::mutex> lock (_mutex);
    logLevel = newInfo.logLevel;
    timeCoord->setInfo (newInfo);
}

void FederateState::createSubscription (Core::Handle handle,
                                        const std::string &key,
                                        const std::string &type,
                                        const std::string &units,
                                        handle_check_mode check_mode)
{
    auto sub = std::make_unique<SubscriptionInfo> (handle, global_id, key, type, units,
                                                   (check_mode == handle_check_mode::required));

    std::lock_guard<std::mutex> lock (_mutex);
    subNames.emplace (key, sub.get ());

    // need to sort the vectors so the find works properly
    if (subscriptions.empty () || handle > subscriptions.back ()->id)
    {
        subscriptions.push_back (std::move (sub));
    }
    else
    {
        subscriptions.push_back (std::move (sub));
        std::sort (subscriptions.begin (), subscriptions.end (), compareFunc);
    }
}
void FederateState::createPublication (Core::Handle handle,
                                       const std::string &key,
                                       const std::string &type,
                                       const std::string &units)
{
    auto pub = std::make_unique<PublicationInfo> (handle, global_id, key, type, units);

    std::lock_guard<std::mutex> lock (_mutex);
    pubNames.emplace (key, pub.get ());

    // need to sort the vectors so the find works properly
    if (publications.empty () || handle > publications.back ()->id)
    {
        publications.push_back (std::move (pub));
    }
    else
    {
        publications.push_back (std::move (pub));
        std::sort (publications.begin (), publications.end (), compareFunc);
    }
}

void FederateState::createEndpoint (Core::Handle handle, const std::string &key, const std::string &type)
{
    auto ep = std::make_unique<EndpointInfo> (handle, global_id, key, type);

    std::lock_guard<std::mutex> lock (_mutex);
    epNames.emplace (key, ep.get ());
    hasEndpoints = true;
    if (endpoints.empty () || handle > endpoints.back ()->id)
    {
        endpoints.push_back (std::move (ep));
    }
    else
    {
        endpoints.push_back (std::move (ep));
        std::sort (endpoints.begin (), endpoints.end (), compareFunc);
    }
}

void FederateState::createSourceFilter (Core::Handle handle,
                                        const std::string &key,
                                        const std::string &target,
                                        const std::string &type)
{
    auto filt = std::make_unique<FilterInfo> (handle, global_id, key, target, type, false);

    std::lock_guard<std::mutex> lock (_mutex);
    filterNames.emplace (key, filt.get ());

    if (filters.empty () || handle > filters.back ()->id)
    {
        filters.push_back (std::move (filt));
    }
    else
    {
        filters.push_back (std::move (filt));
        std::sort (filters.begin (), filters.end (), compareFunc);
    }
}

void FederateState::createDestFilter (Core::Handle handle,
                                      const std::string &key,
                                      const std::string &target,
                                      const std::string &type)
{
    auto filt = std::make_unique<FilterInfo> (handle, global_id, key, target, type, true);

    std::lock_guard<std::mutex> lock (_mutex);
    filterNames.emplace (key, filt.get ());

    if (filters.empty () || handle > filters.back ()->id)
    {
        filters.push_back (std::move (filt));
    }
    else
    {
        filters.push_back (std::move (filt));
        std::sort (filters.begin (), filters.end (), compareFunc);
    }
}

SubscriptionInfo *FederateState::getSubscription (const std::string &subName) const
{
    auto fnd = subNames.find (subName);
    if (fnd != subNames.end ())
    {
        return fnd->second;
    }
    return nullptr;
}

SubscriptionInfo *FederateState::getSubscription (Core::Handle handle_) const
{
    static auto cmptr = [](const std::unique_ptr<SubscriptionInfo> &ptrA, Core::Handle handle) {
        return (ptrA->id < handle);
    };

    auto fnd = std::lower_bound (subscriptions.begin (), subscriptions.end (), handle_, cmptr);
    if (fnd->operator-> ()->id == handle_)
    {
        return fnd->get ();
    }
    return nullptr;
}

PublicationInfo *FederateState::getPublication (const std::string &pubName) const
{
    auto fnd = pubNames.find (pubName);
    if (fnd != pubNames.end ())
    {
        return fnd->second;
    }
    return nullptr;
}

PublicationInfo *FederateState::getPublication (Core::Handle handle_) const
{
    static auto cmptr = [](const std::unique_ptr<PublicationInfo> &ptrA, Core::Handle handle) {
        return (ptrA->id < handle);
    };

    auto fnd = std::lower_bound (publications.begin (), publications.end (), handle_, cmptr);
    if (fnd->operator-> ()->id == handle_)
    {
        return fnd->get ();
    }
    return nullptr;
}

EndpointInfo *FederateState::getEndpoint (const std::string &endpointName) const
{
    auto fnd = epNames.find (endpointName);
    if (fnd != epNames.end ())
    {
        return fnd->second;
    }
    return nullptr;
}

EndpointInfo *FederateState::getEndpoint (Core::Handle handle_) const
{
    static auto cmptr = [](const std::unique_ptr<EndpointInfo> &ptrA, Core::Handle handle) {
        return (ptrA->id < handle);
    };

    auto fnd = std::lower_bound (endpoints.begin (), endpoints.end (), handle_, cmptr);
    if (fnd->operator-> ()->id == handle_)
    {
        return fnd->get ();
    }
    return nullptr;
}

FilterInfo *FederateState::getFilter (const std::string &filterName) const
{
    auto fnd = filterNames.find (filterName);
    if (fnd != filterNames.end ())
    {
        return fnd->second;
    }
    return nullptr;
}

FilterInfo *FederateState::getFilter (Core::Handle handle_) const
{
    static auto cmptr = [](const std::unique_ptr<FilterInfo> &ptrA, Core::Handle handle) {
        return (ptrA->id < handle);
    };

    auto fnd = std::lower_bound (filters.begin (), filters.end (), handle_, cmptr);
    if (fnd->operator-> ()->id == handle_)
    {
        return fnd->get ();
    }
    return nullptr;
}

uint64_t FederateState::getQueueSize (Core::Handle handle_) const
{
    auto epI = getEndpoint (handle_);
    if (epI != nullptr)
    {
        return epI->queueSize (time_granted);
    }
    auto fI = getFilter (handle_);
    if (fI != nullptr)
    {
        return fI->queueSize (time_granted);
    }
    return 0;
}

uint64_t FederateState::getQueueSize () const
{
    uint64_t cnt = 0;
    for (auto &end_point : endpoints)
    {
        cnt += end_point->queueSize (time_granted);
    }
    return cnt;
}

uint64_t FederateState::getFilterQueueSize () const
{
    uint64_t cnt = 0;
    for (auto &filt : filters)
    {
        cnt += filt->queueSize (time_granted);
    }
    return cnt;
}

std::unique_ptr<Message> FederateState::receive (Core::Handle handle_)
{
    auto epI = getEndpoint (handle_);
    if (epI != nullptr)
    {
        return epI->getMessage (time_granted);
    }
    auto fI = getFilter (handle_);
    if (fI != nullptr)
    {
        return fI->getMessage (time_granted);
    }
    return nullptr;
}

std::unique_ptr<Message> FederateState::receiveAny (Core::Handle &id)
{
    Time earliest_time = Time::maxVal ();
    EndpointInfo *endpointI = nullptr;
    // Find the end point with the earliest message time
    for (auto &end_point : endpoints)
    {
        auto t = end_point->firstMessageTime ();
        if (t < earliest_time)
        {
            earliest_time = t;
            endpointI = end_point.get ();
        }
    }

    // Return the message found and remove from the queue
    if (earliest_time <= time_granted)
    {
        auto result = endpointI->getMessage (time_granted);
        id = endpointI->id;
        return result;
    }
    id = invalid_Handle;
    return nullptr;
}

std::unique_ptr<Message> FederateState::receiveAnyFilter (Core::Handle &id)
{
    Time earliest_time = Time::maxVal ();
    FilterInfo *filterI = nullptr;
    // Find the end point with the earliest message time
    for (auto &filt : filters)
    {
        auto t = filt->firstMessageTime ();
        if (t < earliest_time)
        {
            earliest_time = t;
            filterI = filt.get ();
        }
    }

    // Return the message found and remove from the queue
    if (earliest_time <= time_granted)
    {
        auto result = filterI->getMessage (time_granted);
        id = filterI->id;
        return result;
    }
    id = invalid_Handle;
    return nullptr;
}

void FederateState::addAction (const ActionMessage &action)
{
    if (action.action () != CMD_IGNORE)
    {
        queue.push (action);
    }
}

void FederateState::addAction (ActionMessage &&action)
{
    if (action.action () != CMD_IGNORE)
    {
        queue.push (std::move (action));
    }
}

convergence_state FederateState::waitSetup ()
{
    bool expected = false;
    if (processing.compare_exchange_strong (expected, true))
    {  // only enter this loop once per federate
        auto ret = processQueue ();
        processing = false;
        return ret;
    }
    else
    {
        while (!processing.compare_exchange_weak (expected, true))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        convergence_state ret;
        switch (getState ())
        {
        case HELICS_ERROR:
            ret = convergence_state::error;
            break;
        case HELICS_FINISHED:
            ret = convergence_state::halted;
            break;
        default:
            ret = convergence_state::complete;
            break;
        }

        processing = false;
        return ret;
    }
}
/** process until the init state has been entered or there is a failure*/
convergence_state FederateState::enterInitState ()
{
    bool expected = false;
    if (processing.compare_exchange_strong (expected, true))
    {  // only enter this loop once per federate
        auto ret = processQueue ();
        processing = false;
        if (ret == convergence_state::complete)
        {
            time_granted = initialTime;
        }
        return ret;
    }
    else
    {
        while (!processing.compare_exchange_weak (expected, true))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        convergence_state ret;
        switch (getState ())
        {
        case HELICS_ERROR:
            ret = convergence_state::error;
            break;
        case HELICS_FINISHED:
            ret = convergence_state::halted;
            break;
        case HELICS_CREATED:
            // not sure this can actually happen
            ret = convergence_state::nonconverged;
            break;
        default:  // everything >= HELICS_INITIALIZING
            ret = convergence_state::complete;
            break;
        }
        processing = false;
        return ret;
    }
}

convergence_state FederateState::enterExecutingState (convergence_state converged)
{
    bool expected = false;
    if (processing.compare_exchange_strong (expected, true))
    {  // only enter this loop once per federate
        timeCoord->enteringExecMode (converged);
        auto ret = processQueue ();
        if (ret == convergence_state::complete)
        {
            time_granted = timeZero;
        }
        fillEventVector (time_granted);
        processing = false;
        return ret;
    }
    else
    {
        while (!processing.compare_exchange_weak (expected, true))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        convergence_state ret;
        switch (getState ())
        {
        case HELICS_ERROR:
            ret = convergence_state::error;
            break;
        case HELICS_FINISHED:
            ret = convergence_state::halted;
            break;
        case HELICS_CREATED:
        case HELICS_INITIALIZING:
        default:
            ret = convergence_state::nonconverged;
            break;
        case HELICS_EXECUTING:
            ret = convergence_state::complete;
            break;
        }
        processing = false;
        return ret;
    }
}

iterationTime FederateState::requestTime (Time nextTime, convergence_state converged)
{
    bool expected = false;
    if (processing.compare_exchange_strong (expected, true))
    {  // only enter this loop once per federate
        events.clear ();  // clear the event queue
        timeCoord->timeRequest (nextTime, converged, nextValueTime (), nextMessageTime ());
        queue.push (CMD_TIME_CHECK);
        auto ret = processQueue ();
        time_granted = timeCoord->getGrantedTime ();
        iterating = (ret == convergence_state::nonconverged);

        iterationTime retTime = {time_granted, ret};
        // now fill the event vector so exernal systems know what has been updated
        fillEventVector (time_granted);
        processing = false;
        return retTime;
    }
    else
    {
        // this would not be good practice to get into this part of the function
        // but the area must protect itself and should return something sensible
        while (!processing.compare_exchange_weak (expected, true))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        convergence_state ret = iterating ? convergence_state::nonconverged : convergence_state::complete;
        if (state == HELICS_FINISHED)
        {
            ret = convergence_state::halted;
        }
        else if (state == HELICS_ERROR)
        {
            ret = convergence_state::error;
        }
        iterationTime retTime = {time_granted, ret};
        processing = false;
        return retTime;
    }
}

void FederateState::fillEventVector (Time currentTime)
{
    events.clear ();
    for (auto &sub : subscriptions)
    {
        bool updated = sub->updateTime (currentTime);
        if (updated)
        {
            events.push_back (sub->id);
        }
    }
}

convergence_state FederateState::genericUnspecifiedQueueProcess ()
{
    bool expected = false;
    if (processing.compare_exchange_strong (expected, true))
    {  // only 1 thread can enter this loop once per federate
        auto ret = processQueue ();
        time_granted = timeCoord->getGrantedTime ();
        processing = false;
        return ret;
    }
    else
    {
        while (!processing.compare_exchange_weak (expected, true))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        processing = false;
        return convergence_state::complete;
    }
}

const std::vector<Core::Handle> emptyHandles;

const std::vector<Core::Handle> &FederateState::getEvents () const
{
    if (!processing)
    {  //!< if we are processing this vector is in an unstable state
        return events;
    }
    return emptyHandles;
}

convergence_state FederateState::processQueue ()
{
    if (state == HELICS_FINISHED)
    {
        return convergence_state::halted;
    }
    if (state == HELICS_ERROR)
    {
        return convergence_state::error;
    }
    convergence_state ret_code = convergence_state::continue_processing;

    // process the delay Queue first
    if (!delayQueue.empty ())
    {
        // copy the messages over since they could just be placed on the delay queue again
        decltype (delayQueue) tempQueue;
        std::swap (delayQueue, tempQueue);
        while ((ret_code == convergence_state::continue_processing) && (!tempQueue.empty ()))
        {
            auto cmd = tempQueue.front ();
            tempQueue.pop_front ();
            ret_code = processActionMessage (cmd);
        }
        if (ret_code != convergence_state::continue_processing)
        {
            if (!tempQueue.empty ())
            {
                while (!tempQueue.empty ())
                {
                    auto cmd = tempQueue.back ();
                    tempQueue.pop_back ();
                    delayQueue.push_front (cmd);
                }
            }
            return ret_code;
        }
    }

    while (ret_code == convergence_state::continue_processing)
    {
        auto cmd = queue.pop ();
        ret_code = processActionMessage (cmd);
    }
    return ret_code;
}

convergence_state FederateState::processActionMessage (ActionMessage &cmd)
{
    LOG_TRACE ("processing cmd " + prettyPrintString (cmd.action ()));
    switch (cmd.action ())
    {
    case CMD_IGNORE:
    default:
        break;
    case CMD_INIT_GRANT:
        if (state == HELICS_CREATED)
        {
            setState (HELICS_INITIALIZING);
            return convergence_state::complete;
        }
        break;
    case CMD_EXEC_REQUEST:
    case CMD_EXEC_GRANT:
        if (!timeCoord->processTimeMessage (cmd))
        {
            break;
        }
        // FALLTHROUGH
    case CMD_EXEC_CHECK:  // just check the time for entry
    {
        if (state != HELICS_INITIALIZING)
        {
            break;
        }
        auto grant = timeCoord->checkExecEntry ();
        switch (grant)
        {
        case convergence_state::nonconverged:

            return grant;
        case convergence_state::complete:
            setState (HELICS_EXECUTING);
            return grant;
        case convergence_state::continue_processing:
            break;
        default:
            return grant;
        }
    }
    break;
    case CMD_STOP:
    case CMD_DISCONNECT:
        if ((cmd.dest_id == global_id) || (cmd.dest_id == 0))
        {
            setState (HELICS_FINISHED);
            return convergence_state::halted;
        }
        break;
    case CMD_TIME_REQUEST:
    case CMD_TIME_GRANT:
        if (!timeCoord->processTimeMessage (cmd))
        {
            break;
        }
        // FALLTHROUGH
    case CMD_TIME_CHECK:
    {
        if (state != HELICS_EXECUTING)
        {
            break;
        }
        auto ret = timeCoord->checkTimeGrant ();
        if (ret != convergence_state::continue_processing)
        {
            time_granted = timeCoord->getGrantedTime ();
            return ret;
        }
    }
    break;
    case CMD_SEND_MESSAGE:
    {
        auto epi = getEndpoint (cmd.dest_handle);
        if (epi != nullptr)
        {
            timeCoord->updateMessageTime (cmd.actionTime);
            cmd.actionTime += timeCoord->getFedInfo ().impactWindow;
            epi->addMessage (createMessage (std::move (cmd)));
        }
    }
    break;
    case CMD_SEND_FOR_FILTER:
    {
        // this should only be non time_agnostic filters
        auto fI = getFilter (cmd.dest_handle);
        if (fI != nullptr)
        {
            timeCoord->updateMessageTime (cmd.actionTime);
            fI->addMessage (createMessage (std::move (cmd)));
        }
    }
    break;
    case CMD_PUB:
    {
        auto subI = getSubscription (cmd.dest_handle);
        if (subI == nullptr)
        {
            break;
        }
        if (cmd.source_id == subI->target.first)
        {
            subI->addData (cmd.actionTime + timeCoord->getFedInfo ().impactWindow,
                           std::make_shared<const data_block> (std::move (cmd.payload)));
            timeCoord->updateValueTime (cmd.actionTime);
        }
    }
    break;
    case CMD_ERROR:
        setState (HELICS_ERROR);
        return convergence_state::error;
    case CMD_REG_PUB:
    case CMD_NOTIFY_PUB:
    {
        auto subI = getSubscription (cmd.dest_handle);
        if (subI != nullptr)
        {
            subI->target = {cmd.source_id, cmd.source_handle};
            addDependency (cmd.source_id);
        }
    }
    break;
    case CMD_REG_SUB:
    case CMD_NOTIFY_SUB:
    {
        auto pubI = getPublication (cmd.dest_handle);
        if (pubI != nullptr)
        {
            pubI->subscribers.emplace_back (cmd.source_id, cmd.source_handle);
            addDependent (cmd.source_id);
        }
    }
    break;
    case CMD_REG_END:
    case CMD_NOTIFY_END:
    {
        auto filtI = getFilter (cmd.dest_handle);
        if (filtI != nullptr)
        {
            filtI->target = {cmd.source_id, cmd.source_handle};
            addDependency (cmd.source_id);
        }
    }
    break;
    case CMD_ADD_DEPENDENCY:
        if (cmd.dest_id == global_id)
        {
            timeCoord->addDependency (cmd.source_id);
        }

        break;
    case CMD_ADD_DEPENDENT:
        if (cmd.dest_id == global_id)
        {
            timeCoord->addDependent (cmd.source_id);
        }
        break;
    case CMD_REMOVE_DEPENDENCY:
        if (cmd.dest_id == global_id)
        {
            timeCoord->removeDependency (cmd.source_id);
        }
        break;
    case CMD_REMOVE_DEPENDENT:
        if (cmd.dest_id == global_id)
        {
            timeCoord->removeDependent (cmd.source_id);
        }
        break;

    case CMD_REG_DST_FILTER:
    case CMD_NOTIFY_DST_FILTER:
    {
        auto endI = getEndpoint (cmd.dest_handle);
        if (endI != nullptr)
        {
            addDependency (cmd.source_id);
            // todo probably need to do something more here
        }
        break;
    }
    case CMD_REG_SRC_FILTER:
    case CMD_NOTIFY_SRC_FILTER:
    {
        auto endI = getEndpoint (cmd.dest_handle);
        if (endI != nullptr)
        {
            endI->hasFilter = true;
            addDependent (cmd.source_id);
            // todo probably need to do something more here
        }
    }
    break;
    case CMD_FED_ACK:
        if (state != HELICS_CREATED)
        {
            break;
        }
        if (cmd.name == name)
        {
            if (cmd.error)
            {
                setState (HELICS_ERROR);
                return convergence_state::error;
            }
            global_id = cmd.dest_id;
            timeCoord->source_id = global_id;
            return convergence_state::complete;
        }
        break;
    }
    return convergence_state::continue_processing;
}

const std::vector<Core::federate_id_t> &FederateState::getDependents () const
{
    return timeCoord->getDependents ();
}

void FederateState::addDependency (Core::federate_id_t fedToDependOn) { timeCoord->addDependency (fedToDependOn); }

void FederateState::addDependent (Core::federate_id_t fedThatDependsOnThis)
{
    timeCoord->addDependent (fedThatDependsOnThis);
}

Time FederateState::nextValueTime () const
{
    auto firstValueTime = Time::maxVal ();
    for (auto &sub : subscriptions)
    {
        auto nvt = sub->nextValueTime ();
        if (nvt >= time_granted)
        {
            if (nvt < firstValueTime)
            {
                firstValueTime = nvt;
            }
        }
    }
    return firstValueTime;
}

/** find the next Message Event*/
Time FederateState::nextMessageTime () const
{
    auto firstMessageTime = Time::maxVal ();
    for (auto &ep : endpoints)
    {
        auto messageTime = ep->firstMessageTime ();
        if (messageTime >= time_granted)
        {
            if (messageTime < firstMessageTime)
            {
                firstMessageTime = messageTime;
            }
        }
    }
    return firstMessageTime;
}

void FederateState::setCoreObject (CommonCore *parent)
{
    std::lock_guard<std::mutex> lock (_mutex);
    parent_ = parent;
}

void FederateState::logMessage (int level, const std::string &logMessageSource, const std::string &message) const
{
    if ((loggerFunction) && (level <= logLevel))
    {
        loggerFunction (level, (logMessageSource.empty ()) ? name : logMessageSource, message);
    }
}
}