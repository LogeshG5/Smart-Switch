cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(solution_http)

file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS src/*.c)
target_sources(app PRIVATE ${app_sources})

set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/include/generated)
target_include_directories(app PRIVATE
    ${gen_dir}
    src
)

generate_inc_file_for_target(
    app
    web/index.html
    ${gen_dir}/index.html.inc
)

generate_inc_file_for_target(
    app
    web/style.css
    ${gen_dir}/style.css.inc
)

generate_inc_file_for_target(
    app
    web/app.js
    ${gen_dir}/app.js.inc
)

zephyr_linker_sources(SECTIONS src/web/sections-rom.ld)

zephyr_linker_section(
    NAME http_resource_desc_tv_scheduler
    KVMA RAM_REGION
    GROUP RODATA_REGION
)
