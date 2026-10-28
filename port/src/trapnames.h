typedef struct { unsigned short trap; const char *name; int argbytes; int retbytes; } TrapInfo;
extern const TrapInfo trap_info[];
