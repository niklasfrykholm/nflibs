require 'fileutils'

Section = Struct.new(:markdown, :text)
Function = Struct.new(:definition, :doc)

class String
	def indent
		s = self.dup
		return "    " + s.gsub(/\n/, "\n    ")
	end
end

dir = ARGV[0] || '.'
Dir.chdir(dir) do
	FileUtils.mkdir_p('doc')
	Dir["*.c"].each do |cfile|
		mdfile = File.join('doc', cfile)
		mdfile['.c'] = '.md'
		section = Section.new(true, '')
		out = ''
		add_section = lambda do |s|
			s.text.strip!
			out << "\n" if out != ''
			if s.markdown
				out << s.text << "\n"
			else
				out << "```cpp\n" << s.text << "```\n"
			end
		end
		lastblank = true
		in_implementation = false
		functions = []
		IO.foreach(cfile) do |line|
			ismd = line[/^\/\//] || (line.strip == '' && section.markdown)

			if section.markdown && !ismd && line.strip != "" && !lastblank
				functions << Function.new(line.strip, section.text.strip)
			end

			if ismd != section.markdown
				add_section.call(section) unless in_implementation
				section = Section.new(ismd, '')
			end

			if line[/^\/\/ ## Implementation/]
				in_implementation = true
			end

			line[/^\/\/ ?/] = '' if line[/^\/\/ ?/]
			section.text << line
			lastblank = line.strip == ''
		end
		add_section.call(section) unless in_implementation

		out << "\n"
		out << "## Public Functions\n"
		out << "\n"
		functions.each do |f|
			out << "\#\#\# #{f.definition.gsub('*', '\\*')}\n"
			out << "\n"
			out << f.doc << "\n\n"
		end

		File.open(mdfile, "w") do |f|
			f.write(out)
		end
	end
end