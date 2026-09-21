project('gegl-ps-colorize', 'c', version: '0.1')

# 必须启用位置无关代码
#add_project_arguments('-fPIC', language: 'c')

gegl_dep = dependency('gegl-0.4', required: true)

# 构建 GEGL 插件共享模块
shared_module('gegl-ps-colorize',
  sources: ['ps-colorize.c'],
  dependencies: [gegl_dep],
  install: false,
  name_prefix: '',
)