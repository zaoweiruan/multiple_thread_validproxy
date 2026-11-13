# cmake/GetGitVersion.cmake
# 从 Git tag 获取版本信息，回退链保证始终有有效值
#
# 策略:
#   1. 按版本号排序获取最新 v*.*.* tag → 解析 MAJOR.MINOR.PATCH
#   2. 统计 HEAD 与该 tag 之间的提交数
#   3. 检查是否有未提交修改 (dirty)
#   4. 纯 commit hash 回退 (无 tag)
#   5. 默认值 0.0.0 (非 git 环境 / tarball)
#
# 输出变量 (PARENT_SCOPE):
#   VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH  — 版本号组件
#   VERSION_TAG     — 原始 tag 字符串 (如 "v1.4.8" / "v1.4.8-2-gabc1234" / "abc1234")
#   VERSION_COMMIT  — commit hash (空字符串 = 在 tag 上)
#   VERSION_DIRTY   — "0" (干净) 或 "1" (有未提交修改)
#   VERSION_AHEAD   — tag 之后的提交数 (在 tag 上时为 "0")

function(get_git_version)
    find_package(Git QUIET)
    if(NOT Git_FOUND)
        # ---------- 回退: 无 git ----------
        set(VERSION_MAJOR "0" PARENT_SCOPE)
        set(VERSION_MINOR "0" PARENT_SCOPE)
        set(VERSION_PATCH "0" PARENT_SCOPE)
        set(VERSION_TAG "unknown" PARENT_SCOPE)
        set(VERSION_COMMIT "" PARENT_SCOPE)
        set(VERSION_DIRTY "0" PARENT_SCOPE)
        set(VERSION_AHEAD "0" PARENT_SCOPE)
        return()
    endif()

    # ---------- 策略 1: 按版本号排序获取最新 tag ----------
    execute_process(
        COMMAND ${GIT_EXECUTABLE} tag --list "v*" --sort=-version:refname
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG_LIST
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # 取第一行 = 最高版本号 tag
    string(REPLACE "\n" ";" GIT_TAGS "${GIT_TAG_LIST}")
    list(REMOVE_ITEM GIT_TAGS "")
    list(LENGTH GIT_TAGS NUM_TAGS)

    if(NUM_TAGS GREATER 0)
        list(GET GIT_TAGS 0 LATEST_TAG)
    endif()

    if(DEFINED LATEST_TAG AND LATEST_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        set(VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(VERSION_MINOR "${CMAKE_MATCH_2}")
        set(VERSION_PATCH "${CMAKE_MATCH_3}")

        # ---------- 统计自该 tag 以来的提交数 ----------
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count "${LATEST_TAG}..HEAD"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE REV_COUNT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(STRIP "${REV_COUNT}" REV_COUNT)
        if(REV_COUNT STREQUAL "")
            set(REV_COUNT "0")
        endif()

        # ---------- 检查 dirty 状态 ----------
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_STATUS STREQUAL "")
            set(IS_DIRTY "0")
            set(DIRTY_SUFFIX "")
        else()
            set(IS_DIRTY "1")
            set(DIRTY_SUFFIX "-dirty")
        endif()

        # ---------- 构造 VERSION_TAG ----------
        if(REV_COUNT EQUAL 0 AND IS_DIRTY EQUAL 0)
            # 场景 1: 正好在 tag 上，干净
            set(VERSION_TAG "${LATEST_TAG}")
            set(VERSION_COMMIT "")
            set(VERSION_DIRTY "0")
            set(VERSION_AHEAD "0")

        elseif(REV_COUNT EQUAL 0 AND IS_DIRTY EQUAL 1)
            # 场景 2: 在 tag 上但有未提交修改
            set(VERSION_TAG "${LATEST_TAG}${DIRTY_SUFFIX}")
            set(VERSION_COMMIT "")
            set(VERSION_DIRTY "1")
            set(VERSION_AHEAD "0")

        else()
            # 场景 3: tag 之后有提交
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE SHORT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            set(VERSION_TAG "${LATEST_TAG}-${REV_COUNT}-g${SHORT_HASH}${DIRTY_SUFFIX}")
            set(VERSION_COMMIT "${SHORT_HASH}")
            set(VERSION_DIRTY "${IS_DIRTY}")
            set(VERSION_AHEAD "${REV_COUNT}")
        endif()

        _set_all_vars()
        return()

    elseif(GIT_TAG_LIST MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        # ---------- 策略 2: 有 tag 但不完全匹配 (回退) ----------
        string(REGEX MATCH "^v[0-9]+\\.[0-9]+\\.[0-9]+" FIRST_TAG "${GIT_TAG_LIST}")
        if(FIRST_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
            set(VERSION_MAJOR "${CMAKE_MATCH_1}")
            set(VERSION_MINOR "${CMAKE_MATCH_2}")
            set(VERSION_PATCH "${CMAKE_MATCH_3}")
            set(VERSION_TAG "${FIRST_TAG}")
            _set_all_vars()
            return()
        endif()

    else()
        # ---------- 策略 3: 仅有 commit hash ----------
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE HASH_ONLY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        set(VERSION_MAJOR "0")
        set(VERSION_MINOR "0")
        set(VERSION_PATCH "0")
        set(VERSION_TAG "${HASH_ONLY}")
        set(VERSION_COMMIT "${HASH_ONLY}")
        _set_all_vars()
        return()
    endif()

    # 所有路径都 return 了，永不抵达此处
endfunction()

# 辅助: 将局部变量传播到 PARENT_SCOPE
macro(_set_all_vars)
    set(VERSION_MAJOR "${VERSION_MAJOR}" PARENT_SCOPE)
    set(VERSION_MINOR "${VERSION_MINOR}" PARENT_SCOPE)
    set(VERSION_PATCH "${VERSION_PATCH}" PARENT_SCOPE)
    set(VERSION_TAG "${VERSION_TAG}" PARENT_SCOPE)
    set(VERSION_COMMIT "${VERSION_COMMIT}" PARENT_SCOPE)
    set(VERSION_DIRTY "${VERSION_DIRTY}" PARENT_SCOPE)
    set(VERSION_AHEAD "${VERSION_AHEAD}" PARENT_SCOPE)
endmacro()
