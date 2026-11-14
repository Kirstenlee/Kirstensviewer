# -*- cmake -*-
# Modern PowerShell-based message template verification
# Replaces the legacy Python template_verifier.py system

# Find PowerShell 7 (pwsh.exe) or fall back to Windows PowerShell
# This is done globally so PWSH_EXECUTABLE is available to all CMakeLists
# S24 : find_program never re-searches a cached hit; drop it if the exe moved
if(PWSH_EXECUTABLE AND NOT EXISTS "${PWSH_EXECUTABLE}")
  unset(PWSH_EXECUTABLE CACHE)
endif()
# S24 : Store-installed pwsh is only reachable via its WindowsApps execution alias
find_program(PWSH_EXECUTABLE
  NAMES pwsh.exe powershell.exe
  HINTS "$ENV{ProgramFiles}/PowerShell/7"
        "$ENV{ProgramFiles}/PowerShell/7-preview"
        "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps"
  DOC "PowerShell executable"
)

if(NOT PWSH_EXECUTABLE)
  message(WARNING "PowerShell not found - some build steps will be skipped")
endif()

macro (check_message_template_modern _target)
  if(NOT PWSH_EXECUTABLE)
    message(WARNING "PowerShell not found - template verification will be skipped")
    return()
  endif()
  
  # Set default master URL if not provided
  if(NOT DEFINED TEMPLATE_VERIFIER_MASTER_URL)
    set(TEMPLATE_VERIFIER_MASTER_URL 
      "https://github.com/secondlife/master-message-template/raw/master/message_template.msg")
  endif()
  
  # Set verification mode
  if(NOT DEFINED TEMPLATE_VERIFIER_MODE)
    set(TEMPLATE_VERIFIER_MODE "development")
  endif()
  
  add_custom_command(
    TARGET ${_target}
    PRE_LINK
    COMMAND ${PWSH_EXECUTABLE}
      -ExecutionPolicy Bypass
      -NoProfile
      -File "${SCRIPTS_DIR}/Test-MessageTemplate.ps1"
      -TemplateFile "${SCRIPTS_DIR}/messages/message_template.msg"
      -MasterUrl "${TEMPLATE_VERIFIER_MASTER_URL}"
      -Mode "${TEMPLATE_VERIFIER_MODE}"
      -CacheMaster
    COMMENT "Verifying message template compatibility (PowerShell 7)"
    VERBATIM
  )
endmacro (check_message_template_modern)
