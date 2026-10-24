-- Copy the selected xmake target as the complete project-resource bundle.
function main(output)
  import("core.project.config")
  import("core.project.project")
  config.load()
  local target = project.target("hrm-tui")
  assert(target, "Enable the tui option before bundling")
  local source = path.absolute(target:targetfile())
  assert(os.isfile(source), "Build hrm-tui before bundling")
  local destination = path.absolute(output or "build/bundle/hrm-tui")
  os.mkdir(path.directory(destination))
  if source ~= destination then
    os.cp(source, destination)
  end
  print("Bundled TUI: %s", destination)
end
