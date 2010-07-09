/* $Id$ */

/** @file
 *
 * VBox Performance Classes implementation.
 */

/*
 * Copyright (C) 2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * @todo list:
 *
 * 1) Detection of erroneous metric names
 */

#ifndef VBOX_COLLECTOR_TEST_CASE
#include "VirtualBoxImpl.h"
#include "MachineImpl.h"
#endif
#include "Performance.h"

#include <VBox/com/array.h>
#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include <VBox/err.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/cpuset.h>

#include <algorithm>

#include "Logging.h"

using namespace pm;

// Stubs for non-pure virtual methods

int CollectorHAL::getHostCpuLoad(ULONG * /* user */, ULONG * /* kernel */, ULONG * /* idle */)
{
    return E_NOTIMPL;
}

int CollectorHAL::getProcessCpuLoad(RTPROCESS  /* process */, ULONG * /* user */, ULONG * /* kernel */)
{
    return E_NOTIMPL;
}

int CollectorHAL::getRawHostCpuLoad(uint64_t * /* user */, uint64_t * /* kernel */, uint64_t * /* idle */)
{
    return E_NOTIMPL;
}

int CollectorHAL::getRawProcessCpuLoad(RTPROCESS  /* process */, uint64_t * /* user */, uint64_t * /* kernel */, uint64_t * /* total */)
{
    return E_NOTIMPL;
}

int CollectorHAL::getHostMemoryUsage(ULONG * /* total */, ULONG * /* used */, ULONG * /* available */)
{
    return E_NOTIMPL;
}

int CollectorHAL::getProcessMemoryUsage(RTPROCESS /* process */, ULONG * /* used */)
{
    return E_NOTIMPL;
}

int CollectorHAL::enable()
{
    return E_NOTIMPL;
}

int  CollectorHAL::disable()
{
    return E_NOTIMPL;
}

/* Generic implementations */

int CollectorHAL::getHostCpuMHz(ULONG *mhz)
{
    unsigned cCpus = 0;
    uint64_t u64TotalMHz = 0;
    RTCPUSET OnlineSet;
    RTMpGetOnlineSet(&OnlineSet);
    for (RTCPUID iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
    {
        LogAleksey(("{%p} " LOG_FN_FMT ": Checking if CPU %d is member of online set...\n",
                    this, __PRETTY_FUNCTION__, (int)iCpu));
        if (RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
        {
            LogAleksey(("{%p} " LOG_FN_FMT ": Getting frequency for CPU %d...\n",
                        this, __PRETTY_FUNCTION__, (int)iCpu));
            uint32_t uMHz = RTMpGetCurFrequency(RTMpCpuIdFromSetIndex(iCpu));
            if (uMHz != 0)
            {
                LogAleksey(("{%p} " LOG_FN_FMT ": CPU %d %u MHz\n",
                            this, __PRETTY_FUNCTION__, (int)iCpu, uMHz));
                u64TotalMHz += uMHz;
                cCpus++;
            }
        }
    }

    AssertReturn(cCpus, VERR_NOT_IMPLEMENTED);
    *mhz = (ULONG)(u64TotalMHz / cCpus);

    return VINF_SUCCESS;
}

#ifndef VBOX_COLLECTOR_TEST_CASE
CollectorGuestHAL::~CollectorGuestHAL()
{
    Assert(!cEnabled);
}

int CollectorGuestHAL::enable()
{
    HRESULT ret = S_OK;

    if (ASMAtomicIncU32(&cEnabled) == 1)
    {
        ComPtr<IInternalSessionControl> directControl;

        ret = mMachine->getDirectControl(directControl);
        if (ret != S_OK)
            return ret;

        /* get the associated console; this is a remote call (!) */
        ret = directControl->GetRemoteConsole(mConsole.asOutParam());
        if (ret != S_OK)
            return ret;

        ret = mConsole->COMGETTER(Guest)(mGuest.asOutParam());
        if (ret == S_OK)
            mGuest->COMSETTER(StatisticsUpdateInterval)(1 /* 1 sec */);
    }
    return ret;
}

int CollectorGuestHAL::disable()
{
    if (ASMAtomicDecU32(&cEnabled) == 0)
    {
        Assert(mGuest && mConsole);
        mGuest->COMSETTER(StatisticsUpdateInterval)(0 /* off */);
    }
    return S_OK;
}

int CollectorGuestHAL::preCollect(const CollectorHints& /* hints */, uint64_t iTick)
{
    if (    mGuest
        &&  iTick != mLastTick)
    {
        ULONG ulMemAllocTotal, ulMemFreeTotal, ulMemBalloonTotal, ulMemSharedTotal;

        mGuest->InternalGetStatistics(&mCpuUser, &mCpuKernel, &mCpuIdle,
                                      &mMemTotal, &mMemFree, &mMemBalloon, &mMemShared, &mMemCache,
                                      &mPageTotal, &ulMemAllocTotal, &ulMemFreeTotal, &ulMemBalloonTotal, &ulMemSharedTotal);

        if (mHostHAL)
            mHostHAL->setMemHypervisorStats(ulMemAllocTotal, ulMemFreeTotal, ulMemBalloonTotal, ulMemSharedTotal);

        mLastTick = iTick;
    }
    return S_OK;
}

#endif /* !VBOX_COLLECTOR_TEST_CASE */

bool BaseMetric::collectorBeat(uint64_t nowAt)
{
    if (isEnabled())
    {
        if (nowAt - mLastSampleTaken >= mPeriod * 1000)
        {
            mLastSampleTaken = nowAt;
            Log4(("{%p} " LOG_FN_FMT ": Collecting %s for obj(%p)...\n",
                        this, __PRETTY_FUNCTION__, getName(), (void *)mObject));
            return true;
        }
    }
    return false;
}

/*bool BaseMetric::associatedWith(ComPtr<IUnknown> object)
{
    LogFlowThisFunc(("mObject(%p) == object(%p) is %s.\n", mObject, object, mObject == object ? "true" : "false"));
    return mObject == object;
}*/

void HostCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUser->init(mLength);
    mKernel->init(mLength);
    mIdle->init(mLength);
}

void HostCpuLoad::collect()
{
    ULONG user, kernel, idle;
    int rc = mHAL->getHostCpuLoad(&user, &kernel, &idle);
    if (RT_SUCCESS(rc))
    {
        mUser->put(user);
        mKernel->put(kernel);
        mIdle->put(idle);
    }
}

void HostCpuLoadRaw::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostCpuLoad();
}

void HostCpuLoadRaw::collect()
{
    uint64_t user, kernel, idle;
    uint64_t userDiff, kernelDiff, idleDiff, totalDiff;

    int rc = mHAL->getRawHostCpuLoad(&user, &kernel, &idle);
    if (RT_SUCCESS(rc))
    {
        userDiff   = user   - mUserPrev;
        kernelDiff = kernel - mKernelPrev;
        idleDiff   = idle   - mIdlePrev;
        totalDiff  = userDiff + kernelDiff + idleDiff;

        if (totalDiff == 0)
        {
            /* This is only possible if none of counters has changed! */
            LogFlowThisFunc(("Impossible! User, kernel and idle raw "
                "counters has not changed since last sample.\n" ));
            mUser->put(0);
            mKernel->put(0);
            mIdle->put(0);
        }
        else
        {
            mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * userDiff / totalDiff));
            mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * kernelDiff / totalDiff));
            mIdle->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * idleDiff / totalDiff));
        }

        mUserPrev   = user;
        mKernelPrev = kernel;
        mIdlePrev   = idle;
    }
}

void HostCpuMhz::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mMHz->init(mLength);
}

void HostCpuMhz::collect()
{
    ULONG mhz;
    int rc = mHAL->getHostCpuMHz(&mhz);
    if (RT_SUCCESS(rc))
        mMHz->put(mhz);
}

void HostRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mTotal->init(mLength);
    mUsed->init(mLength);
    mAvailable->init(mLength);
    mAllocVMM->init(mLength);
    mFreeVMM->init(mLength);
    mBalloonVMM->init(mLength);
    mSharedVMM->init(mLength);
}

void HostRamUsage::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostRamUsage();
}

void HostRamUsage::collect()
{
    ULONG total, used, available;
    int rc = mHAL->getHostMemoryUsage(&total, &used, &available);
    if (RT_SUCCESS(rc))
    {
        mTotal->put(total);
        mUsed->put(used);
        mAvailable->put(available);

    }
    ULONG allocVMM, freeVMM, balloonVMM, sharedVMM;

    mHAL->getMemHypervisorStats(&allocVMM, &freeVMM, &balloonVMM, &sharedVMM);
    mAllocVMM->put(allocVMM);
    mFreeVMM->put(freeVMM);
    mBalloonVMM->put(balloonVMM);
    mSharedVMM->put(sharedVMM);
}



void MachineCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUser->init(mLength);
    mKernel->init(mLength);
}

void MachineCpuLoad::collect()
{
    ULONG user, kernel;
    int rc = mHAL->getProcessCpuLoad(mProcess, &user, &kernel);
    if (RT_SUCCESS(rc))
    {
        mUser->put(user);
        mKernel->put(kernel);
    }
}

void MachineCpuLoadRaw::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectProcessCpuLoad(mProcess);
}

void MachineCpuLoadRaw::collect()
{
    uint64_t processUser, processKernel, hostTotal;

    int rc = mHAL->getRawProcessCpuLoad(mProcess, &processUser, &processKernel, &hostTotal);
    if (RT_SUCCESS(rc))
    {
        if (hostTotal == mHostTotalPrev)
        {
            /* Nearly impossible, but... */
            mUser->put(0);
            mKernel->put(0);
        }
        else
        {
            mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * (processUser - mProcessUserPrev) / (hostTotal - mHostTotalPrev)));
            mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * (processKernel - mProcessKernelPrev ) / (hostTotal - mHostTotalPrev)));
        }

        mHostTotalPrev     = hostTotal;
        mProcessUserPrev   = processUser;
        mProcessKernelPrev = processKernel;
    }
}

void MachineRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUsed->init(mLength);
}

void MachineRamUsage::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectProcessRamUsage(mProcess);
}

void MachineRamUsage::collect()
{
    ULONG used;
    int rc = mHAL->getProcessMemoryUsage(mProcess, &used);
    if (RT_SUCCESS(rc))
        mUsed->put(used);
}


void GuestCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;

    mUser->init(mLength);
    mKernel->init(mLength);
    mIdle->init(mLength);
}

void GuestCpuLoad::preCollect(CollectorHints& hints, uint64_t iTick)
{
    mHAL->preCollect(hints, iTick);
}

void GuestCpuLoad::collect()
{
    ULONG CpuUser = 0, CpuKernel = 0, CpuIdle = 0;

    mGuestHAL->getGuestCpuLoad(&CpuUser, &CpuKernel, &CpuIdle);
    mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * CpuUser) / 100);
    mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * CpuKernel) / 100);
    mIdle->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * CpuIdle) / 100);
}

void GuestRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;

    mTotal->init(mLength);
    mFree->init(mLength);
    mBallooned->init(mLength);
    mShared->init(mLength);
    mCache->init(mLength);
    mPagedTotal->init(mLength);
}

void GuestRamUsage::preCollect(CollectorHints& hints,  uint64_t iTick)
{
    mHAL->preCollect(hints, iTick);
}

void GuestRamUsage::collect()
{
    ULONG ulMemTotal = 0, ulMemFree = 0, ulMemBalloon = 0, ulMemShared = 0, ulMemCache = 0, ulPageTotal = 0;

    mGuestHAL->getGuestMemLoad(&ulMemTotal, &ulMemFree, &ulMemBalloon, &ulMemShared, &ulMemCache, &ulPageTotal);
    mTotal->put(ulMemTotal);
    mFree->put(ulMemFree);
    mBallooned->put(ulMemBalloon);
    mShared->put(ulMemShared);
    mCache->put(ulMemCache);
    mPagedTotal->put(ulPageTotal);
}

void CircularBuffer::init(ULONG ulLength)
{
    if (mData)
        RTMemFree(mData);
    mLength = ulLength;
    if (mLength)
        mData = (ULONG*)RTMemAllocZ(ulLength * sizeof(ULONG));
    else
        mData = NULL;
    mWrapped = false;
    mEnd = 0;
    mSequenceNumber = 0;
}

ULONG CircularBuffer::length()
{
    return mWrapped ? mLength : mEnd;
}

void CircularBuffer::put(ULONG value)
{
    if (mData)
    {
        mData[mEnd++] = value;
        if (mEnd >= mLength)
        {
            mEnd = 0;
            mWrapped = true;
        }
        ++mSequenceNumber;
    }
}

void CircularBuffer::copyTo(ULONG *data)
{
    if (mWrapped)
    {
        memcpy(data, mData + mEnd, (mLength - mEnd) * sizeof(ULONG));
        // Copy the wrapped part
        if (mEnd)
            memcpy(data + (mLength - mEnd), mData, mEnd * sizeof(ULONG));
    }
    else
        memcpy(data, mData, mEnd * sizeof(ULONG));
}

void SubMetric::query(ULONG *data)
{
    copyTo(data);
}

void Metric::query(ULONG **data, ULONG *count, ULONG *sequenceNumber)
{
    ULONG length;
    ULONG *tmpData;

    length = mSubMetric->length();
    *sequenceNumber = mSubMetric->getSequenceNumber() - length;
    if (length)
    {
        tmpData = (ULONG*)RTMemAlloc(sizeof(*tmpData)*length);
        mSubMetric->query(tmpData);
        if (mAggregate)
        {
            *count = 1;
            *data  = (ULONG*)RTMemAlloc(sizeof(**data));
            **data = mAggregate->compute(tmpData, length);
            RTMemFree(tmpData);
        }
        else
        {
            *count = length;
            *data  = tmpData;
        }
    }
    else
    {
        *count = 0;
        *data  = 0;
    }
}

ULONG AggregateAvg::compute(ULONG *data, ULONG length)
{
    uint64_t tmp = 0;
    for (ULONG i = 0; i < length; ++i)
        tmp += data[i];
    return (ULONG)(tmp / length);
}

const char * AggregateAvg::getName()
{
    return "avg";
}

ULONG AggregateMin::compute(ULONG *data, ULONG length)
{
    ULONG tmp = *data;
    for (ULONG i = 0; i < length; ++i)
        if (data[i] < tmp)
            tmp = data[i];
    return tmp;
}

const char * AggregateMin::getName()
{
    return "min";
}

ULONG AggregateMax::compute(ULONG *data, ULONG length)
{
    ULONG tmp = *data;
    for (ULONG i = 0; i < length; ++i)
        if (data[i] > tmp)
            tmp = data[i];
    return tmp;
}

const char * AggregateMax::getName()
{
    return "max";
}

Filter::Filter(ComSafeArrayIn(IN_BSTR, metricNames),
               ComSafeArrayIn(IUnknown *, objects))
{
    /*
     * Let's work around null/empty safe array mess. I am not sure there is
     * a way to pass null arrays via webservice, I haven't found one. So I
     * guess the users will be forced to use empty arrays instead. Constructing
     * an empty SafeArray is a bit awkward, so what we do in this method is
     * actually convert null arrays to empty arrays and pass them down to
     * init() method. If someone knows how to do it better, please be my guest,
     * fix it.
     */
    if (ComSafeArrayInIsNull(metricNames))
    {
        com::SafeArray<BSTR> nameArray;
        if (ComSafeArrayInIsNull(objects))
        {
            com::SafeIfaceArray<IUnknown> objectArray;
            objectArray.reset(0);
            init(ComSafeArrayAsInParam(nameArray),
                 ComSafeArrayAsInParam(objectArray));
        }
        else
        {
            com::SafeIfaceArray<IUnknown> objectArray(ComSafeArrayInArg(objects));
            init(ComSafeArrayAsInParam(nameArray),
                 ComSafeArrayAsInParam(objectArray));
        }
    }
    else
    {
        com::SafeArray<IN_BSTR> nameArray(ComSafeArrayInArg(metricNames));
        if (ComSafeArrayInIsNull(objects))
        {
            com::SafeIfaceArray<IUnknown> objectArray;
            objectArray.reset(0);
            init(ComSafeArrayAsInParam(nameArray),
                 ComSafeArrayAsInParam(objectArray));
        }
        else
        {
            com::SafeIfaceArray<IUnknown> objectArray(ComSafeArrayInArg(objects));
            init(ComSafeArrayAsInParam(nameArray),
                 ComSafeArrayAsInParam(objectArray));
        }
    }
}

void Filter::init(ComSafeArrayIn(IN_BSTR, metricNames),
                  ComSafeArrayIn(IUnknown *, objects))
{
    com::SafeArray<IN_BSTR> nameArray(ComSafeArrayInArg(metricNames));
    com::SafeIfaceArray<IUnknown> objectArray(ComSafeArrayInArg(objects));

    if (!objectArray.size())
    {
        if (nameArray.size())
        {
            for (size_t i = 0; i < nameArray.size(); ++i)
                processMetricList(com::Utf8Str(nameArray[i]), ComPtr<IUnknown>());
        }
        else
            processMetricList("*", ComPtr<IUnknown>());
    }
    else
    {
        for (size_t i = 0; i < objectArray.size(); ++i)
            switch (nameArray.size())
            {
                case 0:
                    processMetricList("*", objectArray[i]);
                    break;
                case 1:
                    processMetricList(com::Utf8Str(nameArray[0]), objectArray[i]);
                    break;
                default:
                    processMetricList(com::Utf8Str(nameArray[i]), objectArray[i]);
                    break;
            }
    }
}

void Filter::processMetricList(const com::Utf8Str &name, const ComPtr<IUnknown> object)
{
    size_t startPos = 0;

    for (size_t pos = name.find(",");
         pos != com::Utf8Str::npos;
         pos = name.find(",", startPos))
    {
        mElements.push_back(std::make_pair(object, iprt::MiniString(name.substr(startPos, pos - startPos).c_str())));
        startPos = pos + 1;
    }
    mElements.push_back(std::make_pair(object, iprt::MiniString(name.substr(startPos).c_str())));
}

/**
 * The following method was borrowed from stamR3Match (VMM/STAM.cpp) and
 * modified to handle the special case of trailing colon in the pattern.
 *
 * @returns True if matches, false if not.
 * @param   pszPat      Pattern.
 * @param   pszName     Name to match against the pattern.
 * @param   fSeenColon  Seen colon (':').
 */
bool Filter::patternMatch(const char *pszPat, const char *pszName,
                          bool fSeenColon)
{
    /* ASSUMES ASCII */
    for (;;)
    {
        char chPat = *pszPat;
        switch (chPat)
        {
            default:
                if (*pszName != chPat)
                    return false;
                break;

            case '*':
            {
                while ((chPat = *++pszPat) == '*' || chPat == '?')
                    /* nothing */;

                /* Handle a special case, the mask terminating with a colon. */
                if (chPat == ':')
                {
                    if (!fSeenColon && !pszPat[1])
                        return !strchr(pszName, ':');
                    fSeenColon = true;
                }

                for (;;)
                {
                    char ch = *pszName++;
                    if (    ch == chPat
                        &&  (   !chPat
                             || patternMatch(pszPat + 1, pszName, fSeenColon)))
                        return true;
                    if (!ch)
                        return false;
                }
                /* won't ever get here */
                break;
            }

            case '?':
                if (!*pszName)
                    return false;
                break;

            /* Handle a special case, the mask terminating with a colon. */
            case ':':
                if (!fSeenColon && !pszPat[1])
                    return !*pszName;
                if (*pszName != ':')
                    return false;
                fSeenColon = true;
                break;

            case '\0':
                return !*pszName;
        }
        pszName++;
        pszPat++;
    }
    return true;
}

bool Filter::match(const ComPtr<IUnknown> object, const iprt::MiniString &name) const
{
    ElementList::const_iterator it;

    LogAleksey(("Filter::match(%p, %s)\n", static_cast<const IUnknown*> (object), name.c_str()));
    for (it = mElements.begin(); it != mElements.end(); it++)
    {
        LogAleksey(("...matching against(%p, %s)\n", static_cast<const IUnknown*> ((*it).first), (*it).second.c_str()));
        if ((*it).first.isNull() || (*it).first == object)
        {
            // Objects match, compare names
            if (patternMatch((*it).second.c_str(), name.c_str()))
            {
                LogFlowThisFunc(("...found!\n"));
                return true;
            }
        }
    }
    LogAleksey(("...no matches!\n"));
    return false;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
