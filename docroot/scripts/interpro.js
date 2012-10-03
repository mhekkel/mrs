function loadXMLDoc(dname)
{
	if (window.XMLHttpRequest)
	{
		xhttp=new XMLHttpRequest();
	}
	else
	{
		xhttp=new ActiveXObject("Microsoft.XMLHTTP");
	}
	xhttp.open("GET",dname,false);
	xhttp.send("");
	return xhttp.responseXML;
}

Format.toHtml = function(text)
{
	$.get("scripts/interpro.xslt", function(xslt) {
		var xsltProcessor=new XSLTProcessor();
		xsltProcessor.importStylesheet(xslt);
		
		var xmlDoc;
		if (window.DOMParser)
		{
			var parser = new DOMParser();
			xmlDoc = parser.parseFromString(text, "text/xml");
		}
		else // Internet Explorer
		{
			xmlDoc = new ActiveXObject("Microsoft.XMLDOM");
			xmlDoc.async = false;
			xmlDoc.loadXML(text);
		}

		var result = xsltProcessor.transformToFragment(xmlDoc, document);

		document.getElementById("entrytext").appendChild(result);
		
	});
	
/*
	var xsl = loadXMLDoc("scripts/interpro.xslt");
	var xsltProcessor=new XSLTProcessor();
	xsltProcessor.importStylesheet(xsl);
	var resultDocument = xsltProcessor.transformToFragment($(text),document);
	$('<div id="entrytext"/>').append(resultDocument);

			document.getElementById("entrytext").appendChild(resultDocument);
		}
	});

	text = $('<pre/>').html(text);
*/
	return $('<div id="entrytext"/>').html(text);
}
