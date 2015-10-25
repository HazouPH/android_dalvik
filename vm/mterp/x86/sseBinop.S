%default {"instr":"","suff":""}
    movzbl      2(rPC),%ecx                # ecx<- BB
    movzbl      3(rPC),%eax                # eax<- CC
    movs${suff}       (rFP, %ecx, 4),%xmm0       # %xmm0 <- 1st src
    ${instr}${suff}      (rFP,%eax,4),%xmm0
    FETCH_INST_OPCODE 2 %eax
    movs${suff}      %xmm0,(rFP, rINST, 4)  # vAA<- %xmm0
    ADVANCE_PC 2
    GOTO_NEXT_R %eax
