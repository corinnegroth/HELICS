function v = helics_int_property_max_iterations()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 1936535409);
  end
  v = vInitialized;
end
