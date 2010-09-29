/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsAudio class declaration
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBoxVMSettingsAudio_h__
#define __VBoxVMSettingsAudio_h__

#include "UISettingsPage.h"
#include "VBoxVMSettingsAudio.gen.h"
#include "COMDefs.h"

class VBoxVMSettingsAudio : public UISettingsPage,
                            public Ui::VBoxVMSettingsAudio
{
    Q_OBJECT;

public:

    VBoxVMSettingsAudio();

protected:

    void getFrom (const CMachine &aMachine);
    void putBackTo();

    void setOrderAfter (QWidget *aWidget);

    void retranslateUi();

private:

    void prepareComboboxes();

    CMachine mMachine;
};

#endif // __VBoxVMSettingsAudio_h__

