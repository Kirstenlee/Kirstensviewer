# -*- cmake -*-
include(Prebuilt)


use_prebuilt_binary(libhunspell) 
# S24: SLVoice/Vivox retired - voice support disabled
# use_prebuilt_binary(slvoice)
use_prebuilt_binary(nanosvg)
use_prebuilt_binary(emoji_shortcodes)
