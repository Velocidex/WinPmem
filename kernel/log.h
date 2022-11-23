#ifndef _LOG_H_
#define _LOG_H_

#include <ntstrsafe.h>
#include <sal.h>
#include "log_message.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID writeToEventLog(
    _In_                             PDRIVER_OBJECT DriverObject,
    _In_                             UCHAR MajorFunctionCode,
    _In_                             ULONG UniqueErrorValue,
    _In_                             NTSTATUS FinalStatus,
    _In_                             NTSTATUS ErrorCode,
    _In_                             ULONG LengthOfInsert1,
    _In_reads_bytes_opt_(LengthOfInsert1) PWCHAR Insert1,
    _In_                             ULONG LengthOfInsert2,
    _In_reads_bytes_opt_(LengthOfInsert2) PWCHAR Insert2
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS setupEventLogging(_In_ PUNICODE_STRING RegistryPath);

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE,  writeToEventLog)
#pragma alloc_text( PAGE,  setupEventLogging)
#endif

#endif
