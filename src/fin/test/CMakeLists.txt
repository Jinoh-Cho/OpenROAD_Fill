or_integration_tests(
  "fin"
  TESTS
    gcd_fill
  PASSFAIL_TESTS
    aes_density_fill_svg
    aes_fixed_dissection_lp_fill
    gcd_density_fill_svg
    gcd_fixed_dissection_lp_fill
    gcd_fixed_dissection_lp_fill_infeasible
    gcd_tile_grid_density
    ibex_density_fill_svg
    ibex_fixed_dissection_lp_fill
)

add_subdirectory(cpp)
