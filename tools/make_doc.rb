require 'fileutils'
require 'ostruct'

class String
	def indent
		s = self.dup
		return "    " + s.gsub(/\n/, "\n    ")
	end
end

class DocGenerator
	def generate(s)
		groups = group(s.lines.collect {|line| tag(line)})
		out = ""
		groups.each_with_index do |group, i|
			next_group = groups[i+1]
			if group.type == :comment
				out << group.texts.join('') << "\n"
			elsif group.type == :code
				out << "```cpp\n" << group.lines.join('') << "```\n\n"
			end
		end
		out
	end

private
	def tag(line)
		@section = $1 if line[/^\/\/ ## (.*)/]
		OpenStruct.new(
			:line => line,
			:type => line.strip=='' ? :blank :
				line[/^\/\/ ?/] ? :comment : :code,
			:section => @section,
			:text => line.gsub(/^\/\/ ?/, ''),
		)
	end

	def join_code(lines)
		blank = nil
		out = []
		lines.each do |line|
			if line.type == :blank
				blank = line
			elsif line.type == :code
				if blank && out.size>0 && out[-1].type == :code
					blank.type = :code
					out << blank
					blank = nil
				end
				out << line
			elsif line.type == :comment
				out << blank if blank
				blank = nil
				out << line
			end
		end
		out
	end

	def group(lines)
		lines = join_code(lines)
		groups = []
		lines.each_with_index do |l|
			g = groups.size > 0 ? groups[-1] : nil
			if g && g.type == l.type && g.section == l.section
				g[:lines] << l.line
				g[:texts] << l.text
			else
				groups << OpenStruct.new(
					:lines => [l.line],
					:type => l.type,
					:section => l.section,
					:texts => [l.text], 
				)
			end
		end
		groups
	end
end

def create_docs(dir)
	Dir.chdir(dir) do
		FileUtils.mkdir_p('doc')
		Dir["*.c"].each do |cfile|
			doc = DocGenerator.new.generate(IO.read(cfile))
			mdfile = File.join('doc', cfile).gsub('.c', '.md')
			File.open(mdfile, "w") { |f| f.write(doc) }
		end
	end
end

create_docs(ARGV[0] || '..')