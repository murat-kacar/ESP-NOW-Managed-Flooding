# 8. Single building user code

Date: 2026-10-29
Status: Accepted

## Decision
A single 4-digit user code is used for the whole building to open the door from the panel: `* <user_code> #`.
Its factory value is per-device (ADR 0003); the admin changes it.

## Alternative considered
Per-apartment code (`* <apartment_no> * <code> #`): a leak stays limited to one apartment and leaves a trace. Rejected by user preference.

## Accepted risk
- If the code leaks, it must be changed for the whole building and announced to all residents.
- It is not known who opened the door.
