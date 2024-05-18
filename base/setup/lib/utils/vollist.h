/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Volume list functions
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

/* VOLUME UTILITY FUNCTIONS *************************************************/

#if 0
//
// This is the structure from diskpart
//
typedef struct _VOLINFO_OLD
{
    PWSTR pszLabel;
    PWSTR pszFilesystem;
    VOLUME_TYPE VolumeType;
    ULARGE_INTEGER Size;

    PVOLUME_DISK_EXTENTS pExtents;

} VOLINFO_OLD, *PVOLINFO_OLD;

#else

typedef struct _VOLINFO
{
    // WCHAR VolumeName[MAX_PATH]; // Name in the DOS/Win32 namespace: "\??\Volume{GUID}\"
    WCHAR DeviceName[MAX_PATH]; // NT device name: "\Device\HarddiskVolumeN"

    WCHAR DriveLetter;
    WCHAR VolumeLabel[20];
    WCHAR FileSystem[MAX_PATH+1];
} VOLINFO, *PVOLINFO;
#endif

// DetectFileSystem()
NTSTATUS
MountVolume(
    _Inout_ PVOLINFO Volume,
    _In_opt_ UCHAR MbrPartitionType);

NTSTATUS
DismountVolume(
    _Inout_ PVOLINFO Volume);

VOID
AssignDriveLetters(
    _In_ PPARTLIST List);

NTSTATUS
SetMountedDeviceValue(
    _In_ WCHAR Letter,
    _In_ ULONG Signature,
    _In_ LARGE_INTEGER StartingOffset);

/* EOF */
