var Format = {
	name: { value: "generic" },
	
	toHtml: null,
	toFasta: null,
	
	init: function() {
		var html = null;
	
		if (Format.xslt != null) {
			var text = $("#entrytext").text();
			var xslt = Format.loadXMLDoc("scripts/" + Format.xslt);
		
			var result;
			if (window.ActiveXObject) // Internet Explorer
			{
				var xmlDoc = new ActiveXObject("Microsoft.XMLDOM");
				xmlDoc.async = false;
				xmlDoc.loadXML(text);
				result = xmlDoc.transformNode(xslt);
				/* result is a string in IE, strip off the xml declaration
				   or else $.append will fail... */
				result = result.replace(/^<\?xml.+?>/, "").replace(/^\s+<[!?][^>]+>/g);
			}
			else
			{
				var parser = new DOMParser();
				var xmlDoc = parser.parseFromString(text, "text/xml");
				var xsltProcessor = new XSLTProcessor();
				xsltProcessor.importStylesheet(xslt);
				result = xsltProcessor.transformToFragment(xmlDoc, document);
			}
		
			var div = $("<div/>").append(result);
			html = $('<div id="entryhtml"/>').append(div.html());
		}
		
		if (html == null && Format.toHtml != null) {
			html = Format.toHtml($("#entrytext").html());
		}

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
	},
	
	loadXMLDoc: function(dname)
	{
		var xhttp =
			window.XMLHttpRequest ? new XMLHttpRequest() : new ActiveXObject("Microsoft.XMLHTTP");
		xhttp.open("GET", dname, false);
		xhttp.send("");
		
		var result = null;
		
		if (xhttp.responseXML != null)
			result = xhttp.responseXML;
		else if (xhttp.responseText != null)
		{
			if (window.ActiveXObject) // Internet Explorer
			{
				result = new ActiveXObject("Microsoft.XMLDOM");
				result.async = false;
				result.loadXML(xhttp.responseText);
			}
			else
			{
				var parser = new DOMParser();
				result = parser.parseFromString(xhttp.responseText, "text/xml");
			}
		}
		
		return result;
	}
};

addLoadEvent(Format.init);
