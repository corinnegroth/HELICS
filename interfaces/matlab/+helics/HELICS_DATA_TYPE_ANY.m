function v = HELICS_DATA_TYPE_ANY()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 1936535435);
  end
  v = vInitialized;
end
