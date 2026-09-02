or_integration_tests(
  "fin"
  TESTS
    gcd_fill
  PASSFAIL_TESTS
    gcd_fill_rectangle_benchmark
    gcd_fixed_dissection_lp
    gcd_tile_grid_density
    min_var_fill
)

add_subdirectory(cpp)
