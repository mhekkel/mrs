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
				$("#formatSelector option[value='entry']").prop("disabled", false);
				$("#formatSelector option").each(function() {
					$(this).prop("selected", $(this).prop("value") == "entry");
				});
			}
		}
		
		if (Format.toFastA != null) {
			var fasta = Format.toFastA($("#entrytext").html());
			if (html != null) {
				var query = '>' + fasta.id + ' ' + fasta.de + '\n' + fasta.seq;
			
				var html = $('<div id="entryfasta" style="display:none" />').html(
						$("<pre/>").text(query)
					);
				$("#entry").append(html);
			
				$("#formatSelector").prop("disabled", false);
				$("#formatSelector option[value='fasta']").prop("disabled", false);

				$("#blastForm input[name='blast']").prop("disabled", false);
				$("#blastForm input[name='query']").prop("value", query);
			}
		}

		$("#formatSelector").change(function() {
			var fmt = $("#formatSelector option:selected");
			switch ($(fmt).prop("value")) {
				case "entry":
					$("#entrytext").hide();
					$("#entryhtml").show();
					$("#entryfasta").hide();
					break;

				case "fasta":
					$("#entrytext").hide();
					$("#entryhtml").hide();
					$("#entryfasta").show();
					break;
				
				default:
					$("#entrytext").show();
					$("#entryhtml").hide();
					$("#entryfasta").hide();
					break;
			}
		});
	}
};

addLoadEvent(Format.init);
