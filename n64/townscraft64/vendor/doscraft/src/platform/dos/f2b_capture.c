/* Experimental front-to-back item capture. Not connected to the playable
 * renderer yet. Keep the original projected vertices, flags and shade pointer.
 */
#include "f2b_capture.h"

void dos_f2b_capture_reset(struct DosF2BCapture *capture)
{
    if(capture) { capture->count=0; capture->overflow=0; }
}

static int append(struct DosF2BCapture *capture,const RVert *v,int n,
                  const u8 *tile,int flags,int kind,int color)
{
    struct DosF2BCommand *command;
    if(!capture || !v || capture->count<0 || capture->count>DOS_F2B_COMMANDS)
        return -1;
    if(capture->overflow || capture->count==DOS_F2B_COMMANDS) {
        capture->overflow=1; return 0;
    }
    command=&capture->commands[capture->count++];
    command->kind=kind; command->count=n; command->tile=tile;
    command->flags=flags; command->color=color;
    memcpy(command->vertices,v,n*sizeof(*v));
    return 1;
}

int dos_f2b_capture_poly(struct DosF2BCapture *c,const RVert *v,int n,
                         const u8 *tile,int flags)
{
    if(n<3 || n>DOS_F2B_VERTICES || !tile || flags<0 || flags>3) return -1;
    return append(c,v,n,tile,flags,DOS_F2B_TEXTURED,0);
}

int dos_f2b_capture_flat(struct DosF2BCapture *c,const RVert *v,int n,u8 color)
{
    if(n<3 || n>DOS_F2B_VERTICES) return -1;
    return append(c,v,n,NULL,0,DOS_F2B_FLAT,color);
}

int dos_f2b_capture_point(struct DosF2BCapture *c,int x,int y,u8 color)
{
    RVert point={x,y,0,0};
    return append(c,&point,1,NULL,0,DOS_F2B_POINT,color);
}

int dos_f2b_capture_replay(const struct DosF2BCapture *c,int reverse,
                          void (*emit)(const struct DosF2BCommand *,void *),void *context)
{
    int i;
    if(!c || !emit || (reverse!=0 && reverse!=1) || c->count<0 || c->count>DOS_F2B_COMMANDS)
        return -1;
    if(c->overflow) return 0;
    /* Validate the complete item before emitting any prefix. */
    for(i=0;i<c->count;++i) {
        const struct DosF2BCommand *cmd=&c->commands[i];
        if(cmd->color<0 || cmd->color>255) return -1;
        if(cmd->kind==DOS_F2B_POINT) {
            if(cmd->count!=1 || cmd->tile || cmd->flags) return -1;
        } else if(cmd->kind==DOS_F2B_TEXTURED || cmd->kind==DOS_F2B_FLAT) {
            if(cmd->count<3 || cmd->count>DOS_F2B_VERTICES) return -1;
            if(cmd->kind==DOS_F2B_TEXTURED) {
                if(!cmd->tile || cmd->flags<0 || cmd->flags>3) return -1;
            } else if(cmd->tile || cmd->flags) return -1;
        } else return -1;
    }
    for(i=0;i<c->count;++i) emit(&c->commands[reverse ? c->count-1-i : i],context);
    return 1;
}
