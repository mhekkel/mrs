Format.toHtml = function(text) {
	var re1 = /\*FIELD\* (..)\n(((\n|[^*\n].*\n)+|\*(?!FIELD).*\n)+)/g;

	var label = new Array();
	label['AV'] = 'Allelic variation';
	label['CD'] = 'Creation date';
	label['CN'] = 'Contributor name';
	label['CS'] = 'Clinical Synopsis';
	label['ED'] = 'Edit history';
	label['MN'] = 'Mini-Mim';
	label['NO'] = 'Number';
	label['RF'] = 'References';
	label['SA'] = 'See Also';
	label['TI'] = 'Title';
	label['TX'] = 'Text';
	
	var txt = text
		.replace( /\*RECORD\*/, '')
		.replace( /\n\.(\d{4})\n/g, function(s, a) {
			return '\n<a name="' + a + '"/>\n';
		})
		.replace( re1 , function(s, n, t) {
			var ot = t;
			try {
				if (n == 'CS')
					t = '<div style="white-space:pre">' + t + '</div>';
				else if (n == 'CN' || n == 'ED')
					t = '<p>' + t.replace(/\n/g, '<br/>') + '</p>';
				else
					t = '<p>' + t.split('\n\n').join("</p>\n<p>") + '</p>';
	
				return '<h3>' + label[n] + '</h3>\n' + t;
			}
			catch (e) {
				return ot;
			}
		});
	
	return $('<div/>').html(txt);
}
