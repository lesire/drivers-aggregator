module PortListenerPlugin
    attr_accessor :generator
    attr_accessor :registered
    
    class PortListenerGenerator
	attr_accessor :task
	attr_accessor :port_listeners

	def initialize(task)
	    @port_listeners = Hash.new()
	    @task = task
	end
	
	def generate_port_listeners()
	    port_listeners.each do |port_name, gens|
	    
	    port = task.find_port(port_name)
	    if(!port)
		raise "Internal error trying to listen to nonexisting port " + port_name 
	    end
	    
	    puts port_name
		
	    code = "
    #{port.type.cxx_name} #{port_name}Sample;
    while(_#{port_name}.read(#{port_name}Sample) == RTT::NewData )
    {"
	gens.each{|gen| code += gen.call("#{port_name}Sample")}
	code += "
    }"                   
	    task.in_base_hook("update", code)
	    end
	end	
    end
    
    def register_port_listener_generator()
	if(!registered)
	    @generator = PortListenerGenerator.new(self)
	    @registered = true

	    #register code generator for port listeners
	    #note, this needs to be done as early as possible
	    add_generation_handler do
		puts "generation port listenders"
		generator.generate_port_listeners()
	    end	
	end	    
    end
    
    def add_port_listener(name, &generator_method)
	puts "Added port listener for port #{name}"
	if(!registered)
	    raise("port listener generator was not registered prior to calling add_port_listener")	    
	end
	
	if(!generator.port_listeners[name])
	    generator.port_listeners[name] = Array.new
	end
	
	generator.port_listeners[name] << generator_method
    end  
end

module AggregatorPlugin
    include PortListenerPlugin
    
    class AlignedPort
	def initialize(name, period)
	    @port_name = name
	    @data_period = period
	end

	attr_reader :port_name
	attr_reader :data_period
	
	def get_data_type(task)
	    port = task.find_port(port_name)
	    if(!port)
		raise "Error trying to align nonexisting port " + port_name
	    end
	    
	    port.type.cxx_name	    
	end
    end
    
    class AggregatorGenerator
	attr_accessor :task
	attr_reader :agg_name
	
	def initialize(task)
	    @task = task
	    @agg_name = "aggregator"
	end

	def do_parse_time_calls(config)
	    task.property("aggregator_timeout",   'double', config.timeout).
			doc "Maximum time that should be waited for a delayed sample to arrive"
	    puts("Adding property aggregator_timeout")
	    
	    config.aligned_ports.each do |m| 
		#add propertie for adjusting the period if not existing yet
		#this needs to be done now, as the properties must be 
		#present prior to the generation handler call
		property_name = "#{m.port_name}_period"
		if(!(task.find_property(property_name)))
		    task.property(property_name,   'double', m.data_period).
			doc "Time in s between #{m.port_name} readings"
		    puts("Adding property #{property_name}")
		end
		
		index_name = m.port_name + "_idx"
		
		#push data in update hook
		task.add_port_listener(m.port_name) do |sample_name|
		    "
	#{agg_name}.push(#{index_name}, #{sample_name}.time, #{sample_name});"
		end

	    end
	end
	
	
	def generate_aggregator_code(config)
	    
	    task.add_base_header_code("#include<StreamAligner.hpp>", true)
	    task.add_base_member("aggregator", agg_name, "aggregator::StreamAligner")

	    task.in_base_hook("configure", "
    #{agg_name}.setTimeout( base::Time::fromSeconds( _aggregator_timeout.value()) );
	    ")

	    config.aligned_ports.each do |m|     
		callback_name = m.port_name + "Callback"

		port_data_type = m.get_data_type(task)
	                             
		#add callbacks
		task.add_user_method("void", callback_name, "const base::Time &ts, const #{port_data_type} &#{m.port_name}_sample").
		body("    throw std::runtime_error(\"Aggregator callback for #{m.port_name} not implemented\");")

		index_name = m.port_name + "_idx"

		buffer_size_factor = 2.0

		#add variable for index
		task.add_base_member("aggregator", index_name, "int")

		#register callbacks at aggregator
		task.in_base_hook("configure", "
    const double #{m.port_name}Period = _#{m.port_name}_period.value();
    #{index_name} = #{agg_name}.registerStream< #{port_data_type}>(
	boost::bind( &TaskBase::#{callback_name}, this, _1, _2 ),
	#{buffer_size_factor}* ceil( #{config.timeout}/#{m.port_name}Period),
	base::Time::fromSeconds( #{m.port_name}Period ) );
		")

	    end
	    
	    task.in_base_hook("update", "
    while(aggregator.step()) 
    {
	;
    }")

	end
    end
    
    class StreamAlignerConfig
	dsl_attribute :timeout	
	attr_accessor :aligned_ports
	
	def initialize()
	    @aligned_ports = Array.new()
	end
	
	def align_port(name, period)
	    aligned_ports << AlignedPort.new(name, period)
	end	
    end
    
    def add_aggregator(&block)	
	register_port_listener_generator()

	config = StreamAlignerConfig.new()
	config.instance_eval(&block)

	if(!config.timeout)
	   raise "not timeout specified for aggregator" 
	end
    
	generator = AggregatorGenerator.new(self)
	generator.do_parse_time_calls(config)
	
	#register code generator to be called after parsing is done
	add_generation_handler do
	    generator.generate_aggregator_code(config)
	end
    end
end


class Orocos::Generation::TaskContext
  include AggregatorPlugin
end