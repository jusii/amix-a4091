;
; inq.ss -- minimal table-indirect INQUIRY SCRIPTS for the A4091 (NCR 53C710)
;
; Issues one SELECT(ATN) + IDENTIFY + INQUIRY CDB + DATA_IN + STATUS + MSG_IN
; to a quiet target that does NOT disconnect.  Each phase is guarded by a
; JUMP ... WHEN <phase> so an unexpected bus phase yields a clean INT err5
; (DSPS = 0xff05) instead of a hang.  All transfers are table-indirect through
; the DSA, using the same ds_* offsets as siop_script.ss.  DATA_IN uses
; ds_Data1 (DSA offset 0x3c), matching struct siop_ds.
;
; Result vectors returned to the host via INT (read from DSPS):
;   ok          = 0xff00   command complete, success
;   seltimeout  = 0xff10   selection failed / reselected (SELECT's REL target)
;   err5        = 0xff05   unexpected SCSI phase at the dispatch point
;
ARCH 710
;
; --- DSA table offsets (table-indirect), identical to siop_script.ss ---
ABSOLUTE ds_Device	= 0
ABSOLUTE ds_MsgOut	= ds_Device + 4
ABSOLUTE ds_Cmd		= ds_MsgOut + 8
ABSOLUTE ds_Status	= ds_Cmd + 8
ABSOLUTE ds_Msg		= ds_Status + 8
ABSOLUTE ds_MsgIn	= ds_Msg + 8
ABSOLUTE ds_ExtMsg	= ds_MsgIn + 8
ABSOLUTE ds_SyncMsg	= ds_ExtMsg + 8
ABSOLUTE ds_Data1	= ds_SyncMsg + 8
;
; --- result vectors ---
ABSOLUTE ok		= 0xff00
ABSOLUTE seltimeout_v	= 0xff10
ABSOLUTE err5		= 0xff05
;
ENTRY	scripts

PROC	scripts:

scripts:
	SELECT ATN FROM ds_Device, REL(seltimeout)
;
; MESSAGE OUT: send the IDENTIFY byte (ATN already asserted by SELECT)
msgout:
	JUMP REL(err_phase), WHEN NOT MSG_OUT
	MOVE FROM ds_MsgOut, WHEN MSG_OUT
;
; COMMAND: drop ATN, send the 6-byte INQUIRY CDB
command_phase:
	JUMP REL(err_phase), WHEN NOT CMD
	CLEAR ATN
	MOVE FROM ds_Cmd, WHEN CMD
;
; DATA IN: receive the INQUIRY data (one contiguous buffer -> ds_Data1 @ 0x3c)
datain:
	JUMP REL(err_phase), WHEN NOT DATA_IN
	MOVE FROM ds_Data1, WHEN DATA_IN
;
; STATUS: receive the 1 status byte
status_phase:
	JUMP REL(err_phase), WHEN NOT STATUS
	MOVE FROM ds_Status, WHEN STATUS
;
; MESSAGE IN: receive the COMMAND COMPLETE message, ack, wait for disconnect
msgin:
	JUMP REL(err_phase), WHEN NOT MSG_IN
	MOVE FROM ds_Msg, WHEN MSG_IN
	CLEAR ACK
	WAIT DISCONNECT
	INT ok

err_phase:
	INT err5			; unexpected SCSI phase

seltimeout:
	INT seltimeout_v		; selection failed / reselected
