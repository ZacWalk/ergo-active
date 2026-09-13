@{
    schema = 1
    project = @{
        name = 'ergo-active'
        type = 'gui'
        'default-target' = 'app'
    }
    dependencies = @{ owner = 'dd' }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'app'
            kind = 'gui'
            'cmake-target' = 'ergo-active'
            'test-label' = 'ergo-active'
            'debug-path' = 'exe/ergo-active-64d{exe}'
            'release-path' = 'exe/ergo-active-64{exe}'
            platforms = @('x64-windows')
        }
    )
}
