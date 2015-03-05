dir = ARGV[0] || '.'
Dir.chdir(dir) do
	out = ""
	Dir["*.c"].each do |cfile|
		out << "// #{cfile}\n\n"
		interface = false
		IO.foreach(cfile) do |line|
			if line[/\/\/ ## Interface/]
				interface = true
			elsif line[/\/\ ## Implementation/]
				interface = false
			else
				out << line if interface
			end
		end
	end
	File.open('nflibs.h', 'w') do |f|
		f.write(out)
	end
end