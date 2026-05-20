# SPDX-License-Identifier: Apache-2.0

# CLI11 dependency for datasynctool
cli11_dep = dependency('CLI11', required: false)
if not cli11_dep.found()
    cli11_dep = dependency('cli11', required: false)
endif

# Build datasynctool executable
if cli11_dep.found()
    datasynctool_sources = files('main.cpp')

    datasynctool_dependencies = [cli11_dep]

    executable(
        'datasynctool',
        datasynctool_sources,
        dependencies: datasynctool_dependencies,
        include_directories: include_directories('.'),
        install: true,
        install_dir: get_option('bindir'),
    )
else
    warning('CLI11 not found, skipping datasynctool build')
endif
