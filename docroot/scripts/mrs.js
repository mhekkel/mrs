// JavaScript for mrs-web site

function showBox(id, text)
{
	var p = document.getElementById(id);
	var t = document.getElementById(id + '_text');
	
	if (t.style.display == 'none') {
		t.style.display = '';
		p.innerHTML = 'Hide ' + text;
	}
	else {
		t.style.display = 'none';
		p.innerHTML = 'Show ' + text;
	}
}

function selectTab(tab, page, nr)
{
	var tabs = document.getElementById(tab);
	var pages = document.getElementById(page);
	
	for (var i = 0; i < tabs.children.length; ++i) {
		if (i == nr) {
			tabs.children[i].className = 'selected';
			pages.children[i].style.display = '';
		} else {
			tabs.children[i].className = 'unselected';
			pages.children[i].style.display = 'none';
		}
	}
}

// cookie stuff

function Cookie(document, name, hours, path, domain, secure)
{
	this.$document = document;
	this.$name = name;
	if (hours)
	{
		this.$expiration = new Date();
		this.$expiration.setTime(this.$expiration.getTime() + (hours * 60 * 60 * 1000));
	}
	else
		this.$expiration = null;
	if (path) this.$path = path; else this.$path = null;
	if (domain) this.$domain = domain; else this.$domain = null;
	if (secure) this.$secure = secure; else this.$secure = null;
}

Cookie.prototype.store = function()
{
	var cookieval = "";
	for (var prop in this)
	{
		if ((prop.charAt(0) == '$') || ((typeof this[prop]) == 'function') ||
			((typeof this[prop]) == 'undefined') || (prop == 'undefined'))
			continue;
		if (cookieval != "") cookieval += '&';
		cookieval += prop + ':' + escape(this[prop]);
	}
	
	var cookie = this.$name + '=' + cookieval;
	if (this.$expiration)
		cookie += '; expires=' + this.$expiration.toGMTString();
	if (this.$path) cookie += '; path=' + this.$path;
	if (this.$domain) cookie += '; domain=' + this.$domain;
	if (this.$secure) cookie += '; secure';
	
	this.$document.cookie = cookie;
}

Cookie.prototype.load = function()
{
	var allcookies = this.$document.cookie;
	if (allcookies == null || allcookies == "") return false;
	
	var start = allcookies.indexOf(this.$name + '=');
	if (start == -1) return false;
	
	start += this.$name.length + 1;
	var end = allcookies.indexOf(';', start);
	if (end == -1) end = allcookies.length;
	
	var cookieval = allcookies.substring(start, end);
	
	var a = cookieval.split('&');
	for (var i = 0; i < a.length; ++i)
		a[i] = a[i].split(':');
	
	for (var i = 0; i < a.length; ++i)
		this[a[i][0]] = unescape(a[i][1]);
	
	return true;
}

// The global Cookie
var mrsCookie;

// find all page
function updateFindAllStatus()
{
	var c = document.getElementById('chooseOrder');
	if (c == null)
		return;
	
	orderBy(mrsCookie.orderBy);
	
	var c = document.getElementById('chooseCount');
	if (c == null)
		return;

	nrOfHitsToShow(mrsCookie.hitsToShow);
}

function orderBy(by)
{
	if (by != "databank" && by != "relevance")
		by = "databank";

	mrsCookie.orderBy = by;

	var c = document.getElementById('chooseOrder');
	if (c == null)
		return;
	
	for (var i = 0; i < c.children.length; ++i)
	{
		var a = c.children[i];
		if (a.nodeName == "a" && a.text == by)
			a.className = "selected";
		else
			a.className = "";
	}
	
	var table = document.getElementById("tabel");
	var rows = table.rows;
	if (rows == null)
		return;
	
	var rowArray = [];
	for (var i = 1; i < rows.length; ++i)
		rowArray[i - 1] = rows[i];
	
	rowArray.sort(function (a, b) {
		var d = 0;
		
		if (by == 'relevance')
		{
			var scA = a.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "score");
			var scB = b.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "score");
	
			d = scB.value - scA.value;
		}
		
		if (d == 0)
		{
			var dbA = a.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "db");
			var dbB = b.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "db");

			if (dbA.value > dbB.value)
				d = 1;
			else if (dbA.value < dbB.value)
				d = -1;
			else
			{
				var nrA = a.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "hitNr");
				var nrB = b.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "hitNr");
				
				d = nrA.value - nrB.value;
			}
		}
		
		return d;
	});
	
	for (var i = 0; i < rowArray.length; ++i)
		table.appendChild(rowArray[i]);

	delete rowArray;
}

function nrOfHitsToShow(nr, max)
{
	mrsCookie.hitsToShow = nr;
	mrsCookie.store();

	if (nr > max)
	{
		var form = document.getElementById("queryForm");
		form.count.value = nr;
		form.submit();
		return;
	}
	
	var rows = document.getElementById("tabel").rows;
	var db;
	
	for (var d = 1; d < rows.length; ++d)
	{
		var row = rows[d];
		
		var hitNr = row.attributes.getNamedItemNS("http://mrs.cmbi.ru.nl/mrs-web/nl/my-ns", "hitNr");
		
		for (var c = 0; c < row.cells.length; ++c)
		{
			var cell = row.cells[c];
			if (hitNr.value > nr)
				cell.style.display = "none";
			else
				cell.style.display = "";
		}
	}
	
	var c = document.getElementById('chooseCount');
	if (c == null)
		return;

	for (var i = 0; i < c.children.length; ++i)
	{
		var a = c.children[i];
		if (a.nodeName == "a" && a.text == nr)
			a.className = "selected";
		else	
			a.className = "";
	}
}

// --------------------------------------------------------------------
//
// shift click support, for Gert
//

var
	gLastClicked, gClickedWithShift;

function doMouseDownOnCheckbox(event)
{
	gClickedWithShift = event.shiftKey;
	return true;
}

function doClickOnCheckbox(event, list, id)
{
	event.cancelBubble = true;		// will this continue to work???
	if (event.stopPropagation)
		event.stopPropagation(true);

	var cb = document.getElementById(id).firstChild;
	var list = document.getElementById(list).getElementsByTagName('td');
	
	if (cb && cb.checked)
	{
		var ix1, ix2;
		
		ix2 = gLastClicked;
		for (i = 0; i < list.length; ++i)
		{
			if (list[i].id == id || list[i].id == gLastClicked)
			{
				ix1 = i;
				break;
			}
		}
		gLastClicked = ix1;
		
		if (ix1 != ix2 && ix2 >= 0 && gClickedWithShift)
		{
			if (ix1 > ix2)
			{
				var tmp = ix1;
				ix1 = ix2;
				ix2 = tmp;
			}
			
			for (i = ix1; i < ix2; ++i)
				list[i].firstChild.checked = true;
		}
	}
	else
	{
		gLastClicked = -1;
	}

	return true;
}

// --------------------------------------------------------------------
//

function doStopPropagation(event) {
	event.cancelBubble = true;		// will this continue to work???
	if (event.stopPropagation)
		event.stopPropagation(true);
}

// set the onload and onunload functions
function mrsLoad()
{
	mrsCookie = new Cookie(document, "mrs-cookie", 240);

	if (! mrsCookie.load())
	{
		alert("This site uses cookies to store your results. Click OK if you agree.")

		mrsCookie.hitsToShow = 2;
		mrsCookie.orderBy = "databank";
		mrsCookie.store();
	}
}

function addLoadEvent(func) { 
	var oldonload = window.onload; 
	if (typeof window.onload != 'function') { 
		window.onload = func;
	} else {
		window.onload = function() {
			if (oldonload) {
				oldonload(); 
			} 
			func(); 
		} 
	} 
} 

function addUnloadEvent(func) { 
	var oldonunload = window.onload; 
	if (typeof window.onunload != 'function') { 
		window.onunload = func; 
	} else { 
		window.onunload = function() { 
			if (oldonunload) { 
				oldonunload(); 
			}
			func(); 
		} 
	} 
} 

addLoadEvent(mrsLoad);
