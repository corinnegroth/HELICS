function v = no_iteration()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 1936535367);
  end
  v = vInitialized;
end
