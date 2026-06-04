;
; inq-wr.ss -- table-indirect SCSI SCRIPTS for the A4091 (NCR 53C710),
; READ *and* WRITE capable.  Identical to inq.ss except the DATA phase now
; dispatches on the LIVE bus phase the target enters after the command:
;   DATA_OUT -> send  (WRITE)   |   DATA_IN -> receive (READ)   |   none -> STATUS
; Both directions transfer ds_Data1 ({nbyte,addr} from the DSA); the 53C710 MOVE
; direction comes from the phase opcode, so the C side needs no change.
;
ARCH 710
;
; --- DSA table offsets (table-indirect), identical to inq.ss / siop_script.ss ---
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
; COMMAND: drop ATN, send the CDB
command_phase:
	JUMP REL(err_phase), WHEN NOT CMD
	CLEAR ATN
	MOVE FROM ds_Cmd, WHEN CMD
;
; DATA: dispatch on the phase the target actually enters
datadisp:
	JUMP REL(dataout), WHEN DATA_OUT
	JUMP REL(datain), WHEN DATA_IN
	JUMP REL(status_phase), WHEN STATUS	; command with no data phase
	JUMP REL(err_phase)			; anything else
dataout:
	MOVE FROM ds_Data1, WHEN DATA_OUT
	JUMP REL(status_phase)
datain:
	MOVE FROM ds_Data1, WHEN DATA_IN
;
; STATUS: receive the 1 status byte
status_phase:
	JUMP REL(err_phase), WHEN NOT STATUS
	MOVE FROM ds_Status, WHEN STATUS
;
; MESSAGE IN: receive COMMAND COMPLETE, ack, wait for disconnect
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
