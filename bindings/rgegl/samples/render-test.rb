#!/usr/bin/env ruby

$LOAD_PATH.unshift "../src"
$LOAD_PATH.unshift "../src/lib"
require 'gegl'

greyscale=["█", "▓", "▒", "░", " "]
reverse_video=FALSE

width=40
height=40

gegl    = Gegl::Node.new
fractal = gegl.new_child "gegl:fractal_explorer", :width=>width, :height=>height, :ncolors=>3
contrast= gegl.new_child "gegl:threshold", :value=>0.5
text    = gegl.new_child "gegl:text", :string=>'GEGL\n\n term', :size=>height/4
over    = gegl.new_child "gegl:over"

fractal >> contrast >> over
text >> over[:aux]

x=0
buffer = over.render(Gegl::Rectangle.new(0.0,0.0,width,height), 1.0, "Y u8", 0)
pos=0
height.times {|y|
    width.times {|x|
        char = (buffer[pos]/256.0*greyscale.length).to_i
        if reverse_video
            char=greyscale.length-char-1
        end
        # byte by byte to make utf8 work
        greyscale[char].each_byte {|byte| putc byte} 
        pos+=1
    }
    puts ""
}

__END__

expected output:
████████████████████████████████████████
████████████████████████████████████████
████████████████████████████████████████
██▒   ▓██     ███▒   ▓██ ███████████████
█▒░███░██ ██████▒░███░██ ███████████████
█ ███████ ██████ ███████ ███████████████
█ ██   ██     ██ ██   ██ ███████████████
█ ████ ██ ██████ ████ ██ ███████████████
█▒░███ ██ ██████▒░███ ██ ███████████████
██▒   ▓██     ███▒   ▓██     ███████████
████████████████████████████████████████
████████████████████████████████████████
██████████████████  ████████████████████
██████████████████  ████████████████████
███████████████ █     ██████████████████
███████████████         ████████████████
██████████████          ████████████████
██████████████          ████████████████
█████████   █           ████████████████
████████                ████████████████
█                      █████████████████
████████                ████████████████
█████████   █           ████████████████
██████████████          ████████████████
██████████████          ████████████████
███████████████         ████████████████
███████████████ █     ██████████████████
██████████████████  ████████████████████
██████████████████  ████████████████████
████ ███████████████████████████████████
████ ███████████████████████████████████
███    ██░  ▓█ ▒ █ ▒  ▓▒  ▓█████████████
████ ███░▓█▓ █ ▓██ ▒█▓ ▒█▓ █████████████
████ ███     █ ███ ███ ███ █████████████
████ ███░▓████ ███ ███ ███ █████████████
████▒  ██░   █ ███ ███ ███ █████████████
████████████████████████████████████████
████████████████████████████████████████
████████████████████████████████████████
████████████████████████████████████████

