extends Control

var agent: NeedleAgent
var query_thread: Thread
@export var tools: String

func _ready():
	query_thread = Thread.new()
	agent = NeedleAgent.new()
	agent.auto_download = true

	# Connect signals
	agent.loaded.connect(_on_agent_loaded)
	agent.failed.connect(_on_agent_failed)

	# Set up a simple tool
	agent.set_tools(tools)

	# Load the model
	agent.load_model("res://addons/needle_for_godot/models/needle2.cact")

func submit_text():
	var text: String = %LineEdit.text
	if text.is_empty(): return
	%Status.text = "thinking"
	%LineEdit.text = ""

	# Run complete() in a thread to avoid blocking the UI
	query_thread.start(func(): return agent.complete(text))

func _process(_delta: float) -> void:
	if query_thread.is_started() and not query_thread.is_alive():
		var result: Dictionary = query_thread.wait_to_finish()
		var text: String = "Result:\n" + JSON.stringify(result, "  ")
		%Status.text = text

func _on_agent_loaded():
	%Status.text = "Model loaded successfully!"

func _on_agent_failed(error: String):
	%Status.text = "Error: %s" % error
