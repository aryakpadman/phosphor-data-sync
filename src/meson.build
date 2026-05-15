# SPDX-License-Identifier: Apache-2.0

phosphor_dbus_interfaces_dep = dependency('phosphor-dbus-interfaces')
phosphor_logging_dep = dependency('phosphor-logging')
sdbusplus_dep = dependency('sdbusplus')
nlohmann_json_dep = dependency('nlohmann_json')

rbmc_data_sync_sources = [
    files(
        'async_command_exec.cpp',
        'data_sync_config.cpp',
        'data_watcher.cpp',
        'error_log.cpp',
        'external_data_ifaces.cpp',
        'external_data_ifaces_impl.cpp',
        'manager.cpp',
        'notify_service.cpp',
        'notify_sibling.cpp',
        'persistent.cpp',
        'sync_bmc_data_ifaces.cpp',
        'utility.cpp',
    ),
]

rbmc_data_sync_dependencies = [
    phosphor_dbus_interfaces_dep,
    phosphor_logging_dep,
    sdbusplus_dep,
    conf_h_dep,
    nlohmann_json_dep,
]

inc_dir = include_directories('.')
libexecdir_installdir = join_paths(
    get_option('libexecdir'),
    'phosphor-data-sync',
)
executable(
    'phosphor-rbmc-data-sync-mgr',
    'rbmc_data_sync_main.cpp',
    rbmc_data_sync_sources,
    dependencies: rbmc_data_sync_dependencies,
    include_directories: inc_dir,
    install: true,
    install_dir: libexecdir_installdir,
)

# CLI11 dependency for datasynctool
cli11_dep = dependency('CLI11', required: false)
if not cli11_dep.found()
    cli11_dep = dependency('cli11', required: false)
endif

# Build datasynctool executable
if cli11_dep.found()
    datasynctool_dependencies = [
        cli11_dep,
        sdbusplus_dep,
        phosphor_dbus_interfaces_dep,
        nlohmann_json_dep,
    ]

    executable(
        'datasynctool',
        'datasynctool.cpp',
        dependencies: datasynctool_dependencies,
        include_directories: inc_dir,
        install: true,
        install_dir: get_option('bindir'),
    )
else
    warning('CLI11 not found, skipping datasynctool build')
endif
