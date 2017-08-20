fbv 1.1 - Very simple framebuffer displayer very useful for embedded splash screens

Modified the fbv-1.0.b source to fix the following:

 - gifs will display all frames

 - using giflib and not libungif

 - re-organize memory allocs and frees to be consistent

 - frame buffer display redraws background for alpha (useful for gifs)

 - add debug configuration option

 - changes to support better cross-compiling

 - fix formating to use hard tabs everywhere
