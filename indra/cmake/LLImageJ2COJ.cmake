include_guard()

# Always use embedded OpenJPEG - no external library needed
# Consumers don't need OPJ_STATIC since llimagej2coj handles it
set(LLIMAGEJ2COJ_LIBRARIES llimagej2coj)
