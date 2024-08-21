SECTIONS
{
  __percpu_load = .;
  .percpu 0 : AT(__percpu_load) {
    PROVIDE(__percpu_start = .);
    *(.percpu)
    PROVIDE(__percpu_end = .);
  }
  . = __percpu_load + SIZEOF(.percpu);
}
INSERT AFTER .text
