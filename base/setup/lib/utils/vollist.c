/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Volume utilities and list functions
 * COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "precomp.h"

#include "vollist.h"
#include "fsrec.h"
#include "registry.h"

#define NDEBUG
#include <debug.h>

#include <pshpack1.h>
typedef struct _REG_DISK_MOUNT_INFO
{
    ULONG Signature;
    LARGE_INTEGER StartingOffset;
} REG_DISK_MOUNT_INFO, *PREG_DISK_MOUNT_INFO;
#include <poppack.h>


/* FUNCTIONS *****************************************************************/

NTSTATUS
MountVolume(
    _Inout_ PVOLINFO Volume,
    _In_opt_ UCHAR MbrPartitionType)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE VolumeHandle;
    UCHAR LabelBuffer[sizeof(FILE_FS_VOLUME_INFORMATION) + 256 * sizeof(WCHAR)];
    PFILE_FS_VOLUME_INFORMATION LabelInfo = (PFILE_FS_VOLUME_INFORMATION)LabelBuffer;

    /* Try to open the volume so as to mount it */
    RtlInitUnicodeString(&Name, Volume->DeviceName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    VolumeHandle = NULL;
    Status = NtOpenFile(&VolumeHandle,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed, Status 0x%08lx\n", Status);
    }

    if (VolumeHandle)
    {
        ASSERT(NT_SUCCESS(Status));

        /* We don't have a FS, try to guess one */
        Status = InferFileSystem(NULL, VolumeHandle,
                                 Volume->FileSystem,
                                 sizeof(Volume->FileSystem));
        if (!NT_SUCCESS(Status))
            DPRINT1("InferFileSystem() failed, Status 0x%08lx\n", Status);
    }
    if (*Volume->FileSystem)
    {
        ASSERT(VolumeHandle);

        /*
         * Handle volume mounted with RawFS: it is
         * either unformatted or has an unknown format.
         */
        if (wcsicmp(Volume->FileSystem, L"RAW") == 0)
        {
            /*
             * True unformatted partitions on NT are created with their
             * partition type set to either one of the following values,
             * and are mounted with RawFS. This is done this way since we
             * are assured to have FAT support, which is the only FS that
             * uses these partition types. Therefore, having a partition
             * mounted with RawFS and with these partition types means that
             * the FAT FS was unable to mount it beforehand and thus the
             * partition is unformatted.
             * However, any partition mounted by RawFS that does NOT have
             * any of these partition types must be considered as having
             * an unknown format.
             */
            if (MbrPartitionType == PARTITION_FAT_12 ||
                MbrPartitionType == PARTITION_FAT_16 ||
                MbrPartitionType == PARTITION_HUGE   ||
                MbrPartitionType == PARTITION_XINT13 ||
                MbrPartitionType == PARTITION_FAT32  ||
                MbrPartitionType == PARTITION_FAT32_XINT13)
            {
                /* The volume is unformatted */
                NOTHING;
            }
            else
            {
                /* Close the volume before dismounting */
                NtClose(VolumeHandle);
                VolumeHandle = NULL;
                /*
                 * Dismount the volume since RawFS owns it, and reset its
                 * format (it is unknown, may or may not be actually formatted).
                 */
                DismountVolume(Volume);
                *Volume->FileSystem = UNICODE_NULL;
            }
        }
        /* Else, the volume is formatted */
    }
    /* Else, the volume has an unknown format */

    /* Retrieve the volume label */
    if (VolumeHandle)
    {
        Status = NtQueryVolumeInformationFile(VolumeHandle,
                                              &IoStatusBlock,
                                              &LabelBuffer,
                                              sizeof(LabelBuffer),
                                              FileFsVolumeInformation);
        if (NT_SUCCESS(Status))
        {
            /* Copy the (possibly truncated) volume label and NULL-terminate it */
            RtlStringCbCopyNW(Volume->VolumeLabel, sizeof(Volume->VolumeLabel),
                              LabelInfo->VolumeLabel, LabelInfo->VolumeLabelLength);
        }
        else
        {
            DPRINT1("NtQueryVolumeInformationFile() failed, Status 0x%08lx\n", Status);
        }
    }

    /* Close the volume */
    if (VolumeHandle)
        NtClose(VolumeHandle);

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Attempts to dismount the designated volume.
 **/
NTSTATUS
DismountVolume(
    _Inout_ PVOLINFO Volume)
{
    NTSTATUS Status;
    NTSTATUS LockStatus;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE VolumeHandle;

    /* Check whether the volume was mounted by the system.
     * If the volume is not mounted, just return success. */
    if (!*Volume->FileSystem)
        return STATUS_SUCCESS;

    /* Open the volume */
    RtlInitUnicodeString(&Name, Volume->DeviceName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&VolumeHandle,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERROR: Cannot open volume %wZ for dismounting! (Status 0x%lx)\n",
                &Name, Status);
        return Status;
    }

    /* Lock the volume */
    LockStatus = NtFsControlFile(VolumeHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_LOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("WARNING: Failed to lock volume! Operations may fail! (Status 0x%lx)\n", LockStatus);
    }

    /* Dismount the volume */
    Status = NtFsControlFile(VolumeHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_DISMOUNT_VOLUME,
                             NULL,
                             0,
                             NULL,
                             0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to unmount volume (Status 0x%lx)\n", Status);
    }

    /* Unlock the volume */
    LockStatus = NtFsControlFile(VolumeHandle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 FSCTL_UNLOCK_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0);
    if (!NT_SUCCESS(LockStatus))
    {
        DPRINT1("Failed to unlock volume (Status 0x%lx)\n", LockStatus);
    }

    /* Close the volume */
    NtClose(VolumeHandle);

    /* Zero out some data only if dismount succeeded */
    if (NT_SUCCESS(Status))
    {
        Volume->DriveLetter = UNICODE_NULL;
        // *Volume->VolumeLabel = UNICODE_NULL;
        RtlZeroMemory(Volume->VolumeLabel, sizeof(Volume->VolumeLabel));
        // *Volume->FileSystem = UNICODE_NULL;
        RtlZeroMemory(Volume->FileSystem, sizeof(Volume->FileSystem));
    }

    return Status;
}

/*
 * FIXME: Rely on the MOUNTMGR to assign the drive letters.
 *
 * For the moment, we do it ourselves, by assigning drives to partitions
 * that are *only on MBR disks*. We first assign letters to each active
 * partition on each disk, then assign letters to each logical partition,
 * and finish by assigning letters to the remaining primary partitions.
 * (This algorithm is the one that can be observed in the Windows Setup.)
 */
VOID
AssignDriveLetters(
    _In_ PPARTLIST List)
{
    PDISKENTRY DiskEntry;
    PPARTENTRY PartEntry;
    PLIST_ENTRY Entry1;
    PLIST_ENTRY Entry2;
    WCHAR Letter;

    Letter = L'C';

    /* Assign drive letters to primary partitions */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        for (Entry2 = DiskEntry->PrimaryPartListHead.Flink;
             Entry2 != &DiskEntry->PrimaryPartListHead;
             Entry2 = Entry2->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);

            PartEntry->Volume->Info.DriveLetter = 0;

            if (PartEntry->IsPartitioned &&
                !IsContainerPartition(PartEntry->PartitionType) &&
                (IsRecognizedPartition(PartEntry->PartitionType) ||
                 PartEntry->SectorCount.QuadPart != 0LL))
            {
                if (Letter <= L'Z')
                    PartEntry->Volume->Info.DriveLetter = Letter++;
            }
        }
    }

    /* Assign drive letters to logical drives */
    for (Entry1 = List->DiskListHead.Flink;
         Entry1 != &List->DiskListHead;
         Entry1 = Entry1->Flink)
    {
        DiskEntry = CONTAINING_RECORD(Entry1, DISKENTRY, ListEntry);

        for (Entry2 = DiskEntry->LogicalPartListHead.Flink;
             Entry2 != &DiskEntry->LogicalPartListHead;
             Entry2 = Entry2->Flink)
        {
            PartEntry = CONTAINING_RECORD(Entry2, PARTENTRY, ListEntry);

            PartEntry->Volume->Info.DriveLetter = 0;

            if (PartEntry->IsPartitioned &&
                (IsRecognizedPartition(PartEntry->PartitionType) ||
                 PartEntry->SectorCount.QuadPart != 0LL))
            {
                if (Letter <= L'Z')
                    PartEntry->Volume->Info.DriveLetter = Letter++;
            }
        }
    }
}

/**
 * @brief
 * Assign a "\DosDevices\#:" mount point drive letter to a disk partition or
 * volume, specified by a given disk signature and starting partition offset.
 *
 * @note
 * The association is stored in the registry of the **TARGET**
 * NT installation, not of the current running one.
 *
 * We use it to update the mounted devices list
 * // FIXME: This should technically be done by mountmgr (if AutoMount is enabled)!
 *
 * TODO:
 * - Make the function more generic for MBR and GPT;
 * - It may actually make sense to just migrate the volume letters database
 *   from the current active system to the installation TARGET one (when both
 *   are for the same machine).
 *
 * @note
 * The stored data actually corresponds to a "unique ID" the partition manager
 * gives to the mount manager. In this function below, the format is actually
 * the one used for partitions on MBR disks only.
 *
 * @see
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/supporting-mount-manager-requests-in-a-storage-class-driver
 * https://winreg-kb.readthedocs.io/en/latest/sources/system-keys/Mounted-devices.html
 **/
NTSTATUS
SetMountedDeviceValue(
    _In_ WCHAR Letter,
    _In_ ULONG Signature, // DiskSignature
    _In_ LARGE_INTEGER StartingOffset)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"SYSTEM\\MountedDevices");
    UNICODE_STRING ValueName;
    WCHAR ValueNameBuffer[16];
    HANDLE KeyHandle;
    REG_DISK_MOUNT_INFO MountInfo;

    RtlStringCchPrintfW(ValueNameBuffer, _countof(ValueNameBuffer),
                        L"\\DosDevices\\%c:", Letter);
    RtlInitUnicodeString(&ValueName, ValueNameBuffer);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_LOCAL_MACHINE, NULL),
                               NULL);

    Status = NtOpenKey(&KeyHandle,
                       KEY_ALL_ACCESS,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        Status = NtCreateKey(&KeyHandle,
                             KEY_ALL_ACCESS,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return Status;
    }

    MountInfo.Signature = Signature;
    MountInfo.StartingOffset = StartingOffset;
    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_BINARY,
                           (PVOID)&MountInfo,
                           sizeof(MountInfo));
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

/* EOF */
