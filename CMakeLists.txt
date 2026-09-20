cmake_minimum_required(VERSION 3.20)

project(
    ps1shooter
    LANGUAGES    C CXX ASM
    VERSION      1.0.0
    DESCRIPTION  "PS1 space shooter"
)

psn00bsdk_add_executable(shooter GPREL main.c)
psn00bsdk_add_cd_image(iso shooter iso.xml DEPENDS shooter ${PROJECT_SOURCE_DIR}/system.cnf)
