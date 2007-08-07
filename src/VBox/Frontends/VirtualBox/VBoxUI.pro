#
# VBox GUI: additional Qt project file (for Qt Designer).
#
# NOTE: This file is intended to be opened by Qt Designer
#       as a project file (to work with .ui files)
#

#
#  Copyright (C) 2006-2007 innotek GmbH
# 
#  This file is part of VirtualBox Open Source Edition (OSE), as
#  available from http://www.virtualbox.org. This file is free software;
#  you can redistribute it and/or modify it under the terms of the GNU
#  General Public License as published by the Free Software Foundation,
#  in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
#  distribution. VirtualBox OSE is distributed in the hope that it will
#  be useful, but WITHOUT ANY WARRANTY of any kind.

TEMPLATE	= app
LANGUAGE	= C++

FORMS	= ui/VBoxDiskImageManagerDlg.ui \
	ui/VBoxVMNetworkSettings.ui \
	ui/VBoxUSBFilterSettings.ui \
	ui/VBoxSharedFoldersSettings.ui \
	ui/VBoxNewVMWzd.ui \
	ui/VBoxCloseVMDlg.ui \
	ui/VBoxTakeSnapshotDlg.ui \
	ui/VBoxVMSettingsDlg.ui \
	ui/VBoxSnapshotDetailsDlg.ui \
	ui/VBoxGlobalSettingsDlg.ui \
	ui/VBoxNewHDWzd.ui \
	ui/VBoxSnapshotsWgt.ui \
	ui/VBoxAboutDlg.ui \
	ui/VBoxVMFirstRunWzd.ui \
	ui/VBoxVMLogViewer.ui

IMAGES	= images/tpixel.png \
	images/vm_new.png \
	images/vm_new_dis.png \
	images/vm_settings.png \
	images/vm_settings_dis.png \
	images/vm_delete.png \
	images/vm_delete_dis.png \
	images/vm_start.png \
	images/vm_start_dis.png \
	images/vm_discard.png \
	images/vm_discard_dis.png \
	images/vm_pause_s.png \
	images/vm_pause_16px.png \
	images/os_freebsd.png \
	images/os_openbsd.png \
	images/os_netbsd.png \
	images/os_dos.png \
	images/os_linux.png \
	images/os_l4.png \
	images/os_netware.png \
	images/os_os2.png \
	images/os_other.png \
	images/os_solaris.png \
	images/os_win2000.png \
	images/os_win2003.png \
	images/os_win31.png \
	images/os_win95.png \
	images/os_win98.png \
	images/os_winme.png \
	images/os_winnt.png \
	images/os_winvista.png \
	images/os_winxp.png \
	images/state_aborted_16px.png \
	images/state_discarding_16px.png \
	images/state_paused_16px.png \
	images/state_running_16px.png \
	images/state_saved_16px.png \
	images/state_saving_16px.png \
	images/state_powered_off_16px.png \
	images/state_restoring_16px.png \
	images/offline_snapshot_16px.png \
	images/online_snapshot_16px.png \
	images/vmw_new_welcome.png \
	images/vmw_new_harddisk.png \
	images/fd_16px.png \
	images/fd_read_16px.png \
	images/fd_write_16px.png \
	images/fd_disabled_16px.png \
	images/cd_16px.png \
	images/cd_read_16px.png \
	images/cd_write_16px.png \
	images/cd_disabled_16px.png \
	images/cd_unmount_16px.png \
	images/cd_unmount_dis_16px.png \
	images/hd_16px.png \
	images/hd_read_16px.png \
	images/hd_write_16px.png \
	images/hd_disabled_16px.png \
	images/mouse_16px.png \
	images/mouse_disabled_16px.png \
	images/mouse_seamless_16px.png \
	images/mouse_can_seamless_16px.png \
	images/mouse_can_seamless_disabled_16px.png \
	images/mouse_can_seamless_uncaptured_16px.png \
	images/hostkey_16px.png \
	images/hostkey_pressed_16px.png \
	images/hostkey_captured_16px.png \
	images/hostkey_captured_pressed_16px.png \
	images/hostkey_disabled_16px.png \
	images/machine_16px.png \
	images/ico16x01.png \
	images/ico20x01.png \
	images/ico32x01.png \
	images/ico40x01.png \
	images/ico64x01.png \
	images/help_16px.png \
	images/about_16px.png \
	images/site_16px.png \
	images/reset_16px.png \
	images/reset_disabled_16px.png \
	images/pause_16px.png \
	images/fullscreen_16px.png \
	images/fullscreen_disabled_16px.png \
	images/acpi_16px.png \
	images/acpi_disabled_16px.png \
	images/adjust_win_size_16px.png \
	images/adjust_win_size_disabled_16px.png \
	images/auto_resize_on_16px.png \
	images/auto_resize_on_disabled_16px.png \
	images/auto_resize_off_16px.png \
	images/auto_resize_off_disabled_16px.png \
	images/exit_16px.png \
	images/fd_unmount_16px.png \
	images/fd_unmount_dis_16px.png \
	images/guesttools_16px.png \
	images/guesttools_disabled_16px.png \
	images/diskim_16px.png \
	images/settings_16px.png \
	images/delete_16px.png \
	images/new_16px.png \
	images/start_16px.png \
	images/start_dis_16px.png \
	images/discard_16px.png \
	images/discard_dis_16px.png \
	images/settings_dis_16px.png \
	images/delete_dis_16px.png \
	images/sound_16px.png \
	images/nw_16px.png \
	images/nw_read_16px.png \
	images/nw_write_16px.png \
	images/nw_disabled_16px.png \
	images/usb_16px.png \
	images/usb_new_16px.png \
	images/usb_new_disabled_16px.png \
	images/usb_disabled_16px.png \
	images/usb_read_16px.png \
	images/usb_write_16px.png \
	images/usb_add_16px.png \
	images/usb_add_disabled_16px.png \
	images/usb_movedown_16px.png \
	images/usb_movedown_disabled_16px.png \
	images/usb_moveup_16px.png \
	images/usb_moveup_disabled_16px.png \
	images/usb_remove_16px.png \
	images/usb_remove_disabled_16px.png \
	images/usb_unavailable_16px.png \
	images/usb_unavailable_disabled_16px.png \
	images/select_file_16px.png \
	images/select_file_dis_16px.png \
	images/list_moveup_16px.png \
	images/list_moveup_22px.png \
	images/list_moveup_disabled_16px.png \
	images/list_moveup_disabled_22px.png \
	images/list_movedown_16px.png \
	images/list_movedown_22px.png \
	images/list_movedown_disabled_16px.png \
	images/list_movedown_disabled_22px.png \
	images/discard_cur_state_16px.png \
	images/discard_cur_state_22px.png \
	images/discard_cur_state_dis_16px.png \
	images/discard_cur_state_dis_22px.png \
	images/discard_cur_state_snapshot_16px.png \
	images/discard_cur_state_snapshot_22px.png \
	images/discard_cur_state_snapshot_dis_16px.png \
	images/discard_cur_state_snapshot_dis_22px.png \
	images/discard_snapshot_16px.png \
	images/discard_snapshot_22px.png \
	images/discard_snapshot_dis_16px.png \
	images/discard_snapshot_dis_22px.png \
	images/take_snapshot_16px.png \
	images/take_snapshot_22px.png \
	images/take_snapshot_dis_16px.png \
	images/take_snapshot_dis_22px.png \
	images/show_snapshot_details_16px.png \
	images/show_snapshot_details_22px.png \
	images/show_snapshot_details_dis_16px.png \
	images/show_snapshot_details_dis_22px.png \
	images/add_shared_folder_16px.png \
	images/add_shared_folder_disabled_16px.png \
	images/edit_shared_folder_16px.png \
	images/edit_shared_folder_disabled_16px.png \
	images/revome_shared_folder_16px.png \
	images/revome_shared_folder_disabled_16px.png \
	images/shared_folder_16px.png \
	images/shared_folder_disabled_16px.png \
	images/shared_folder_read_16px.png \
	images/shared_folder_write_16px.png \
	images/add_host_iface_16px.png \
	images/add_host_iface_disabled_16px.png \
	images/remove_host_iface_16px.png \
	images/remove_host_iface_disabled_16px.png \
	images/eraser_16px.png \
	images/eraser_disabled_16px.png \
	images/refresh_16px.png \
	images/refresh_disabled_16px.png \
	images/refresh_22px.png \
	images/refresh_disabled_22px.png \
	images/show_logs_16px.png \
	images/show_logs_22px.png \
	images/show_logs_disabled_16px.png \
	images/show_logs_disabled_22px.png \
	images/description_16px.png \
	images/description_disabled_16px.png \
	images/edit_description_16px.png \
	images/edit_description_disabled_16px.png \
	images/global_settings_16px.png \
	images/global_settings_diasbled_16px.png \
	images/vrdp_16px.png \
	images/vrdp_disabled_16px.png \
	images/vdm_add_16px.png \
	images/vdm_add_disabled_16px.png \
	images/vdm_add_22px.png \
	images/vdm_add_disabled_22px.png \
	images/vdm_new_16px.png \
	images/vdm_new_disabled_16px.png \
	images/vdm_new_22px.png \
	images/vdm_new_disabled_22px.png \
	images/vdm_release_16px.png \
	images/vdm_release_disabled_16px.png \
	images/vdm_release_22px.png \
	images/vdm_release_disabled_22px.png \
	images/vdm_remove_16px.png \
	images/vdm_remove_disabled_16px.png \
	images/vdm_remove_22px.png \
	images/vdm_remove_disabled_22px.png \
	images/welcome.png \
	images/about.png \
	images/about_tile.png \
	images/dock_0.png \
	images/dock_1.png \
    images/dock_128x128_transparent.png

TRANSLATIONS = \
	nls/VirtualBox_hu.ts \
	nls/VirtualBox_fi.ts \
	nls/VirtualBox_ko.ts \
	nls/VirtualBox_sv.ts \
	nls/VirtualBox_pt_BR.ts \
	nls/VirtualBox_ja.ts \
	nls/VirtualBox_pl.ts \
	nls/VirtualBox_ar.ts \
	nls/VirtualBox_de.ts \
	nls/VirtualBox_es.ts \
	nls/VirtualBox_fr.ts \
	nls/VirtualBox_it.ts \
	nls/VirtualBox_ro.ts \
	nls/VirtualBox_ru.ts \
	nls/VirtualBox_zh_CN.ts \
	nls/VirtualBox_zh_TW.ts

