option(USE_GSF             "Build with libGSF" ON)
if(USE_GSF AND MIXERX_LGPL)
    if(USE_SYSTEM_AUDIO_LIBRARIES)
        find_package(GSF QUIET)
        message("GSF: [${GSF_FOUND}] ${GSF_INCLUDE_DIRS} ${GSF_LIB}")
    endif()

    if(GSF_FOUND)
        message("== using libGSF ==")
        if(DOWNLOAD_AUDIO_CODECS_DEPENDENCY)
            setLicense(LICENSE_GPL_2p)  # at AudioCodecs set, the MAME YM2612 emualtor is enabled by default
        else()
            setLicense(LICENSE_LGPL_2_1p)
        endif()

        list(APPEND SDL_MIXER_DEFINITIONS -DMUSIC_GSF)
        list(APPEND SDL_MIXER_INCLUDE_PATHS ${GSF_INCLUDE_DIRS})

        if(NOT USE_SYSTEM_AUDIO_LIBRARIES)
            list(APPEND SDLMixerX_LINK_LIBS ${GSF_LIB})
        endif()

        list(APPEND SDLMixerX_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/music_gsf.c
            ${CMAKE_CURRENT_LIST_DIR}/music_gsf.h
        )

        appendChiptuneFormats("MINIGSF")
    else()
        message("-- skipping GSF --")
    endif()
endif()
