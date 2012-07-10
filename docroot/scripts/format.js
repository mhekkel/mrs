var Format = {
	name: { value: "generic" },
	
	toHtml: null,
	toFasta: null,
	
	init: function() {
		if (Format.toHtml != null) {
			var html = Format.toHtml($("#entrytext").html());
			if (html != null) {
				html.prop("id", "entryhtml");

				$("#entry").prepend(html);
				$("#entrytext").hide();
				$("#formatSelector").prop("disabled", false);
				$("#formatSelector option").each(function() {
					$(this).prop("selected", $(this).prop("value") == "entry");
				});
				$("#formatSelector").change(function() {
					var fmt = $("#formatSelector option:selected");
					switch ($(fmt).prop("value")) {
						case "entry":
							$("#entrytext").hide();
							$("#entryhtml").show();
							break;
						
						default:
							$("#entrytext").show();
							$("#entryhtml").hide();
							break;
					}
				});
			}
		}
	}
};

addLoadEvent(Format.init);
