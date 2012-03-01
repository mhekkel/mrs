/*
	Functions for the multiple sequence alignment results page.
*/

function toggleClustalColors() {
	var d = document.getElementById('alignment');
	var l = document.getElementById('msa_show_hide_button');

	if (d.className == 'msa') {
		d.className = 'msa-nocolour';
		if (l.value != null) {
			l.value = 'show colours';
		} else {
			l.innerHTML = 'show colours';
		}
	}
	else {
		d.className = 'msa';
		l.innerHTML = 'hide colours';
	}
}

function toggleClustalWrap() {
	var w = document.getElementById('wrapped');
	var u = document.getElementById('unwrapped');
	var l = document.getElementById('msa_wrap_button');

	if (w.style.display == 'none') {
		w.style.display = '';
		u.style.display = 'none';
		l.innerHTML = 'show unwrapped';
	}
	else {
		w.style.display = 'none';
		u.style.display = '';
		l.innerHTML = 'show wrapped';
	}
}

function submitAlignJob() {
	var form = document.getElementById('alignForm');

	jQuery.post("ajax/align/submit", { input: form.input.value },
		function(result, status) {
			jQuery('#waiting').hide();

			if (status == "success") {
				if (result.error != null && result.error.length > 0) {
					alert("Retrieving align results failed:\n" + result.error);
				}
				if (result.alignment != null && result.alignment.length > 0) {
					displayAlignResults(result.alignment);
				}
			} else {
				alert('Error receiving alignment results');
			}
		}, "json");
	
	// let the user know we're working
	jQuery('#waiting').show();
	jQuery('#result').hide();
}

function displayAlignResults(s) {
	var d = document.getElementById('result');
	d.style.display = '';

	var r = new Array();
	var r2 = new Array();

	s = unescape(s);

	var re = />(.+)\n/m;
	var a = s.split(/(?=>)/);

	for (i in a) {
		var m = re.exec(a[i]);
		if (m == null) {
			continue;
		}
		var seq = a[i].substr(m[0].length).replace(/\n/g, '');
		seq = seq.replace(/([DE]+)/g, "<span class='c1'>$1</span>");
		seq = seq.replace(/([KR]+)/g, "<span class='c2'>$1</span>");
		seq = seq.replace(/([H]+)/g, "<span class='c3'>$1</span>");
		seq = seq.replace(/([NQ]+)/g, "<span class='c4'>$1</span>");
		seq = seq.replace(/([CM]+)/g, "<span class='c5'>$1</span>");
		seq = seq.replace(/([GP]+)/g, "<span class='c6'>$1</span>");
		seq = seq.replace(/([AVLIWF]+)/g, "<span class='c7'>$1</span>");
		seq = seq.replace(/([YST]+)/g, "<span class='c8'>$1</span>");
	
		var id = m[1].substr(0, 15);
		id += '                '.substr(id.length);
	
		var al = { id: id, a: seq };
		r.push(al);

		// for wrapped version
		var aa = new Array();
		seq = a[i].substr(m[0].length).replace(/\n/g, '');
		
		for (var j = 0; j < seq.length; j += 60) {
			var s2 = seq.substr(j, 60);
			s2 = s2.replace(/([DE]+)/g, "<span class='c1'>$1</span>");
			s2 = s2.replace(/([KR]+)/g, "<span class='c2'>$1</span>");
			s2 = s2.replace(/([H]+)/g, "<span class='c3'>$1</span>");
			s2 = s2.replace(/([NQ]+)/g, "<span class='c4'>$1</span>");
			s2 = s2.replace(/([CM]+)/g, "<span class='c5'>$1</span>");
			s2 = s2.replace(/([GP]+)/g, "<span class='c6'>$1</span>");
			s2 = s2.replace(/([AVLIWF]+)/g, "<span class='c7'>$1</span>");
			s2 = s2.replace(/([YST]+)/g, "<span class='c8'>$1</span>");
			aa.push(s2);
		}
		
		r2.push(aa);
	}
	
	s = '';
	for (i in r) {
		s += r[i].id;
		s += r[i].a;
		s += '\n';
	}
	
	document.getElementById('unwrapped').innerHTML = s;

	s = '';
	
	for (var j = 0; j < r2[0].length; ++j) {
		for (i in r) {
			s += r[i].id;
			s += r2[i][j];
			s += '\n';
		}
		s += '\n';
	}

	document.getElementById('wrapped').innerHTML = s;
}

function initAlignPage() {
	var form = document.getElementById('alignForm');
	var fasta = form.input.value;

	if (fasta.length > 0) {
		submitAlignJob();
	}
}

addLoadEvent(initAlignPage);
