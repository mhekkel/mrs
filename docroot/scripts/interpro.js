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
	
		text = text.replace(/&amp;/, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>');
	
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
		
		var result;
		if (window.ActiveXObject)
		{
			result = xmlDoc.transformNode(xslt);
		}
		// code for Mozilla, Firefox, Opera, etc.
		else
		{
			var xsltProcessor=new XSLTProcessor();
			xsltProcessor.importStylesheet(xslt);
			result = xsltProcessor.transformToFragment(xmlDoc, document);
		}
		
		$("#entryhtml").replaceWith(result);
	});

	return $('<div id="entrytext"/>').html(text);
}
