HANDLE_OPCODE(OP_INVOKE_OBJECT_INIT_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    {
        if (self->interpBreak.ctl.subMode & kSubModeDebuggerActive) {
            /* behave like OP_INVOKE_DIRECT_RANGE */
            GOTO_invoke(invokeDirect, true);
        }
        FINISH(3);
    }
OP_END
