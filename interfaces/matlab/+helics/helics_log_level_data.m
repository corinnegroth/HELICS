function v = helics_log_level_data()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 1946183076);
  end
  v = vInitialized;
end