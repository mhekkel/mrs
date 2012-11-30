var Format = {
	name: { value: "generic" },
	
	db: null,
	id: null,
	fasta: null,
	
	toHtml: null,
	
	init: function() {
		var html = null;
	
		if (Format.xslt != null) {
			var text = $("#entrytext").text();
			var xslt = Format.loadXMLDoc(Format.xslt);
		
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
			html = div.html();
		}
		
		if (html == null && Format.toHtml != null) {
			html = Format.toHtml($("#entrytext").html());
		}

		if (html != null) {
			$("#entrytext").hide();
			$("#entryhtml").html(html).show();
			$("#formatSelector").prop("disabled", false);
			$("#formatSelector option[value='entry']").prop("disabled", false);
			$("#formatSelector option").each(function() {
				$(this).prop("selected", $(this).prop("value") == "entry");
			});
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
					if (Format.fasta == null)
						Format.loadFasta();
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
	},
	
	loadFasta: function()
	{
		jQuery.get("rest/entry/" + Format.db + '/' + Format.id + "?format=fasta", function(data) {
			$("#entryfasta").show().html($("<pre/>").append(data));
		});
	}
};

addLoadEvent(Format.init);
