/* libnx supplies .eh_frame_hdr bounds; NativeAOT's bare-metal unwinder also
   needs the complete DWARF frame section. Keep all definitions image-relative. */
PROVIDE_HIDDEN(__eh_frame_start = ADDR(.eh_frame));
PROVIDE_HIDDEN(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));
